//===-- EmulateInstructionRX.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "EmulateInstructionRX.h"

#include "lldb/Core/PluginManager.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/DataExtractor.h"

#include "llvm/Support/MathExtras.h"

#include <algorithm>
#include <iterator>

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE_ADV(EmulateInstructionRX, InstructionRX)

namespace {
// DWARF register numbers (as in ABISysV_rx); the unwind plans use them.
enum dwarf_regnums {
  dwarf_r0 = 0, // the stack pointer
  dwarf_r6 = 6, // the frame pointer, when there is one
  dwarf_psw = 16,
  dwarf_pc = 17,
};
} // namespace

#define REG(name, alt, num, generic)                                           \
  {#name,                                                                      \
   alt,                                                                        \
   4,                                                                          \
   0,                                                                          \
   eEncodingUint,                                                              \
   eFormatHex,                                                                 \
   {num, num, generic, LLDB_INVALID_REGNUM, num},                              \
   nullptr,                                                                    \
   nullptr,                                                                    \
   nullptr}

// Indexed by the DWARF number.
static const RegisterInfo g_register_infos[] = {
    REG(r0, "sp", 0, LLDB_REGNUM_GENERIC_SP),
    REG(r1, nullptr, 1, LLDB_REGNUM_GENERIC_ARG1),
    REG(r2, nullptr, 2, LLDB_REGNUM_GENERIC_ARG2),
    REG(r3, nullptr, 3, LLDB_REGNUM_GENERIC_ARG3),
    REG(r4, nullptr, 4, LLDB_REGNUM_GENERIC_ARG4),
    REG(r5, nullptr, 5, LLDB_INVALID_REGNUM),
    REG(r6, "fp", 6, LLDB_REGNUM_GENERIC_FP),
    REG(r7, nullptr, 7, LLDB_INVALID_REGNUM),
    REG(r8, nullptr, 8, LLDB_INVALID_REGNUM),
    REG(r9, nullptr, 9, LLDB_INVALID_REGNUM),
    REG(r10, nullptr, 10, LLDB_INVALID_REGNUM),
    REG(r11, nullptr, 11, LLDB_INVALID_REGNUM),
    REG(r12, nullptr, 12, LLDB_INVALID_REGNUM),
    REG(r13, nullptr, 13, LLDB_INVALID_REGNUM),
    REG(r14, nullptr, 14, LLDB_INVALID_REGNUM),
    REG(r15, nullptr, 15, LLDB_INVALID_REGNUM),
    REG(psw, nullptr, 16, LLDB_REGNUM_GENERIC_FLAGS),
    REG(pc, nullptr, 17, LLDB_REGNUM_GENERIC_PC),
};

#undef REG

EmulateInstructionRX::EmulateInstructionRX(const ArchSpec &arch)
    : EmulateInstruction(arch) {}

void EmulateInstructionRX::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance);
}

void EmulateInstructionRX::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

llvm::StringRef EmulateInstructionRX::GetPluginDescriptionStatic() {
  return "Emulate the prologue and epilogue instructions of Renesas RX.";
}

EmulateInstruction *
EmulateInstructionRX::CreateInstance(const ArchSpec &arch,
                                     InstructionType inst_type) {
  if (SupportsEmulatingInstructionsOfTypeStatic(inst_type) &&
      arch.GetTriple().getArch() == llvm::Triple::rx)
    return new EmulateInstructionRX(arch);
  return nullptr;
}

bool EmulateInstructionRX::SetTargetTriple(const ArchSpec &arch) {
  return arch.GetTriple().getArch() == llvm::Triple::rx;
}

std::optional<RegisterInfo>
EmulateInstructionRX::GetRegisterInfo(RegisterKind reg_kind, uint32_t reg_num) {
  if (reg_kind == eRegisterKindGeneric) {
    switch (reg_num) {
    case LLDB_REGNUM_GENERIC_SP:
      reg_num = dwarf_r0;
      break;
    case LLDB_REGNUM_GENERIC_FP:
      reg_num = dwarf_r6;
      break;
    case LLDB_REGNUM_GENERIC_PC:
      reg_num = dwarf_pc;
      break;
    case LLDB_REGNUM_GENERIC_FLAGS:
      reg_num = dwarf_psw;
      break;
    default:
      return {};
    }
  } else if (reg_kind != eRegisterKindDWARF &&
             reg_kind != eRegisterKindEHFrame && reg_kind != eRegisterKindLLDB) {
    return {};
  }
  if (reg_num >= std::size(g_register_infos))
    return {};
  return g_register_infos[reg_num];
}

// Only for unwinding: UnwindAssemblyInstEmulation hands over the instructions
// from the disassembler, so the emulator never reads them itself.
bool EmulateInstructionRX::ReadInstruction() { return false; }

// On entry the call has pushed the return address: CFA = SP + 4, PC at
// CFA - 4 (as ABISysV_rx's function-entry plan).
bool EmulateInstructionRX::CreateFunctionEntryUnwind(UnwindPlan &unwind_plan) {
  m_fp_set = false;
  unwind_plan.Clear();
  unwind_plan.SetRegisterKind(eRegisterKindDWARF);

  UnwindPlan::Row row;
  row.GetCFAValue().SetIsRegisterPlusOffset(dwarf_r0, 4);
  row.SetRegisterLocationToAtCFAPlusOffset(dwarf_pc, -4, true);
  row.SetRegisterLocationToIsCFAPlusOffset(dwarf_r0, 0, true);

  unwind_plan.AppendRow(std::move(row));
  unwind_plan.SetSourceName("EmulateInstructionRX");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolNo);
  unwind_plan.SetUnwindPlanValidAtAllInstructions(eLazyBoolYes);
  unwind_plan.SetUnwindPlanForSignalTrap(eLazyBoolNo);
  return true;
}

uint8_t EmulateInstructionRX::Byte(size_t i) const {
  return i < m_size ? m_bytes[i] : 0;
}

int32_t EmulateInstructionRX::Signed(size_t i, size_t n) const {
  uint32_t v = 0;
  for (size_t k = 0; k < n; ++k)
    v |= uint32_t(Byte(i + k)) << (8 * k);
  return n == 4 ? int32_t(v) : llvm::SignExtend32(v, 8 * n);
}

bool EmulateInstructionRX::ReadSP(uint32_t &sp) {
  bool success = false;
  sp = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0, 0, &success);
  return success;
}

bool EmulateInstructionRX::WriteSP(ContextType type, uint32_t sp) {
  Context ctx;
  ctx.type = type;
  ctx.SetNoArgs();
  return WriteRegisterUnsigned(ctx, eRegisterKindDWARF, dwarf_r0, sp);
}

// Stores `reg` below SP (push.l, and each register of pushm).
bool EmulateInstructionRX::Push(uint32_t reg, uint32_t &sp) {
  std::optional<RegisterInfo> reg_info =
      GetRegisterInfo(eRegisterKindDWARF, reg);
  std::optional<RegisterInfo> sp_info =
      GetRegisterInfo(eRegisterKindDWARF, dwarf_r0);
  if (!reg_info || !sp_info)
    return false;
  bool success = false;
  uint32_t value = ReadRegisterUnsigned(*reg_info, 0, &success);
  if (!success)
    return false;
  sp -= 4;
  Context ctx;
  ctx.type = eContextPushRegisterOnStack;
  ctx.SetRegisterToRegisterPlusOffset(*reg_info, *sp_info, 0);
  return WriteMemoryUnsigned(ctx, sp, value, 4);
}

// Loads `reg` from SP (pop, and each register of popm).
bool EmulateInstructionRX::Pop(uint32_t reg, uint32_t &sp) {
  std::optional<RegisterInfo> reg_info =
      GetRegisterInfo(eRegisterKindDWARF, reg);
  if (!reg_info)
    return false;
  Context ctx;
  ctx.type = eContextPopRegisterOffStack;
  ctx.SetAddress(sp);
  bool success = false;
  uint32_t value = ReadMemoryUnsigned(ctx, sp, 4, 0, &success);
  if (!success)
    return false;
  sp += 4;
  return WriteRegisterUnsigned(ctx, *reg_info, value);
}

// pushm Rs-Rd: Rd goes to the highest address, Rs to the new SP.
bool EmulateInstructionRX::EmulatePushm(uint8_t rs, uint8_t rd) {
  uint32_t sp;
  if (rs == 0 || rs > rd || !ReadSP(sp))
    return false;
  for (uint32_t r = rd + 1; r-- > rs;)
    if (!Push(r, sp))
      return false;
  return WriteSP(eContextAdjustStackPointer, sp);
}

bool EmulateInstructionRX::EmulatePopm(uint8_t rs, uint8_t rd) {
  uint32_t sp;
  if (rs == 0 || rs > rd || !ReadSP(sp))
    return false;
  for (uint32_t r = rs; r <= rd; ++r)
    if (!Pop(r, sp))
      return false;
  return WriteSP(eContextAdjustStackPointer, sp);
}

// add #imm, Rs, Rd (and add / sub #uimm4, Rd with Rs = Rd): SP adjustments
// (Rs = Rd = R0) and setting the frame pointer (add #-N, r0, r6).
bool EmulateInstructionRX::EmulateAddToSP(uint8_t rs, uint8_t rd,
                                          int32_t imm) {
  if (rs != dwarf_r0)
    return false;
  uint32_t sp;
  if (!ReadSP(sp))
    return false;
  if (rd == dwarf_r0)
    return WriteSP(eContextAdjustStackPointer, sp + imm);
  if (rd != dwarf_r6)
    return false;
  std::optional<RegisterInfo> fp_info =
      GetRegisterInfo(eRegisterKindDWARF, dwarf_r6);
  if (!fp_info)
    return false;
  Context ctx;
  ctx.type = eContextSetFramePointer;
  ctx.SetRegister(*fp_info);
  m_fp_set = true;
  return WriteRegisterUnsigned(ctx, *fp_info, sp + imm);
}

// mov.l r0, r6 sets the frame pointer; mov.l r6, r0 sets SP from it.
bool EmulateInstructionRX::EmulateMov(uint8_t rs, uint8_t rd) {
  if (rs == dwarf_r0 && rd == dwarf_r6)
    return EmulateAddToSP(rs, rd, 0);
  if (rs != dwarf_r6 || rd != dwarf_r0 || !m_fp_set)
    return false;
  bool success = false;
  uint32_t fp = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r6, 0, &success);
  return success && WriteSP(eContextAdjustStackPointer, fp);
}

// The displacement is from the branch instruction itself.
bool EmulateInstructionRX::EmulateBranch(int32_t disp) {
  bool success = false;
  uint32_t pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc, 0, &success);
  if (!success)
    return false;
  Context ctx;
  ctx.type = eContextRelativeBranchImmediate;
  ctx.SetImmediateSigned(disp);
  return WriteRegisterUnsigned(ctx, eRegisterKindDWARF, dwarf_pc, pc + disp);
}

bool EmulateInstructionRX::EvaluateInstruction(uint32_t evaluate_options) {
  DataExtractor data;
  m_size = std::min<size_t>(m_opcode.GetData(data), sizeof(m_bytes));
  if (m_size == 0 || data.CopyData(0, m_size, m_bytes) != m_size)
    return false;

  const uint8_t b0 = Byte(0), b1 = Byte(1);
  const uint8_t hi = b1 >> 4, lo = b1 & 0xf;
  uint32_t sp;

  // bra.s / beq.s / bne.s: 3 bits of displacement, 3 to 10.
  if (b0 >= 0x08 && b0 <= 0x1f) {
    uint32_t dsp = b0 & 7;
    return EmulateBranch(dsp < 3 ? dsp + 8 : dsp);
  }
  // bcnd.b / bra.b
  if (b0 >= 0x20 && b0 <= 0x2f)
    return EmulateBranch(Signed(1, 1));

  switch (b0) {
  case 0x04: // bra.a
    return EmulateBranch(Signed(1, 3));
  case 0x38: // bra.w
  case 0x3a: // beq.w
  case 0x3b: // bne.w
    return EmulateBranch(Signed(1, 2));
  case 0x60: // sub #uimm4, Rd
    return lo == dwarf_r0 && EmulateAddToSP(dwarf_r0, dwarf_r0, -int32_t(hi));
  case 0x62: // add #uimm4, Rd
    return lo == dwarf_r0 && EmulateAddToSP(dwarf_r0, dwarf_r0, hi);
  case 0x6e: // pushm Rs-Rd
    return EmulatePushm(hi, lo);
  case 0x6f: // popm Rs-Rd
    return EmulatePopm(hi, lo);
  case 0x70: // add #imm32, Rs, Rd
    return EmulateAddToSP(hi, lo, Signed(2, 4));
  case 0x71: // add #simm8, Rs, Rd
  case 0x72: // add #simm16, Rs, Rd
  case 0x73: // add #simm24, Rs, Rd
    return EmulateAddToSP(hi, lo, Signed(2, b0 & 3));
  case 0x7e:
    switch (hi) {
    case 0x8: // push.b Rs
    case 0x9: // push.w Rs: SP moves by 4 for every size
      return ReadSP(sp) && WriteSP(eContextAdjustStackPointer, sp - 4);
    case 0xa: // push.l Rs
      return lo != dwarf_r0 && ReadSP(sp) && Push(lo, sp) &&
             WriteSP(eContextAdjustStackPointer, sp);
    case 0xb: // pop Rd
      return lo != dwarf_r0 && ReadSP(sp) && Pop(lo, sp) &&
             WriteSP(eContextAdjustStackPointer, sp);
    case 0xc: // pushc CR: PSW has a DWARF number, the others do not
      if (lo == 0)
        return ReadSP(sp) && Push(dwarf_psw, sp) &&
               WriteSP(eContextAdjustStackPointer, sp);
      return ReadSP(sp) && WriteSP(eContextAdjustStackPointer, sp - 4);
    case 0xe: // popc CR
      if (lo == 0)
        return ReadSP(sp) && Pop(dwarf_psw, sp) &&
               WriteSP(eContextAdjustStackPointer, sp);
      return ReadSP(sp) && WriteSP(eContextAdjustStackPointer, sp + 4);
    }
    return false;
  case 0xef: // mov.l Rs, Rd
    return EmulateMov(hi, lo);
  }
  // rts, rtsd and rte end the function's path (the disassembler marks them
  // as barriers); calls do not change the frame.
  return false;
}
