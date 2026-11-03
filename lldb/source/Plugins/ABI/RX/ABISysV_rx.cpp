//===-- ABISysV_rx.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABISysV_rx.h"

#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Value.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Status.h"
#include "lldb/ValueObject/ValueObjectConstResult.h"

#include "llvm/TargetParser/Triple.h"

#include <cstring>

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE_ADV(ABISysV_rx, ABIRX)

// DWARF register numbers, as the RX backend (and GCC) emits them.
enum dwarf_regnums {
  dwarf_r0 = 0, // the stack pointer
  dwarf_r1,
  dwarf_r2,
  dwarf_r3,
  dwarf_r4,
  dwarf_r5,
  dwarf_r6, // the frame pointer, when there is one
  dwarf_r7,
  dwarf_r8,
  dwarf_r9,
  dwarf_r10,
  dwarf_r11,
  dwarf_r12,
  dwarf_r13,
  dwarf_r14,
  dwarf_r15,
  dwarf_psw,
  dwarf_pc,
  dwarf_dr0 = 32,
};

#define REG(name, alt, size, dwarf, generic, encoding, format)                 \
  {#name, alt,     size,    0, encoding, format,                               \
   {dwarf, dwarf, generic, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},          \
   nullptr, nullptr, nullptr}
#define GPR(n, generic)                                                        \
  REG(r##n, nullptr, 4, dwarf_r##n, generic, eEncodingUint, eFormatHex)
#define CTL(name, generic)                                                     \
  REG(name, nullptr, 4, LLDB_INVALID_REGNUM, generic, eEncodingUint,           \
      eFormatHex)
#define DR(n)                                                                  \
  REG(dr##n, nullptr, 8, dwarf_dr0 + n, LLDB_INVALID_REGNUM,                   \
      eEncodingIEEE754, eFormatFloat)

// The names are those of GDB's org.gnu.gdb.rx.core feature (which QEMU and
// Renesas e2-server-gdb use), so that a remote's registers get these numbers.
static const RegisterInfo g_register_infos[] = {
    REG(r0, "sp", 4, dwarf_r0, LLDB_REGNUM_GENERIC_SP, eEncodingUint,
        eFormatHex),
    GPR(1, LLDB_REGNUM_GENERIC_ARG1),
    GPR(2, LLDB_REGNUM_GENERIC_ARG2),
    GPR(3, LLDB_REGNUM_GENERIC_ARG3),
    GPR(4, LLDB_REGNUM_GENERIC_ARG4),
    GPR(5, LLDB_INVALID_REGNUM),
    REG(r6, "fp", 4, dwarf_r6, LLDB_REGNUM_GENERIC_FP, eEncodingUint,
        eFormatHex),
    GPR(7, LLDB_INVALID_REGNUM),
    GPR(8, LLDB_INVALID_REGNUM),
    GPR(9, LLDB_INVALID_REGNUM),
    GPR(10, LLDB_INVALID_REGNUM),
    GPR(11, LLDB_INVALID_REGNUM),
    GPR(12, LLDB_INVALID_REGNUM),
    GPR(13, LLDB_INVALID_REGNUM),
    GPR(14, LLDB_INVALID_REGNUM),
    GPR(15, LLDB_INVALID_REGNUM),
    CTL(usp, LLDB_INVALID_REGNUM),
    CTL(isp, LLDB_INVALID_REGNUM),
    REG(psw, nullptr, 4, dwarf_psw, LLDB_REGNUM_GENERIC_FLAGS, eEncodingUint,
        eFormatHex),
    REG(pc, nullptr, 4, dwarf_pc, LLDB_REGNUM_GENERIC_PC, eEncodingUint,
        eFormatHex),
    CTL(intb, LLDB_INVALID_REGNUM),
    CTL(bpsw, LLDB_INVALID_REGNUM),
    CTL(bpc, LLDB_INVALID_REGNUM),
    CTL(fintv, LLDB_INVALID_REGNUM),
    CTL(fpsw, LLDB_INVALID_REGNUM),
    DR(0),
    DR(1),
    DR(2),
    DR(3),
    DR(4),
    DR(5),
    DR(6),
    DR(7),
    DR(8),
    DR(9),
    DR(10),
    DR(11),
    DR(12),
    DR(13),
    DR(14),
    DR(15),
};

#undef DR
#undef CTL
#undef GPR
#undef REG

const RegisterInfo *ABISysV_rx::GetRegisterInfoArray(uint32_t &count) {
  count = std::size(g_register_infos);
  return g_register_infos;
}

ABISP ABISysV_rx::CreateInstance(ProcessSP process_sp, const ArchSpec &arch) {
  if (arch.GetTriple().getArch() == llvm::Triple::rx)
    return ABISP(
        new ABISysV_rx(std::move(process_sp), MakeMCRegisterInfo(arch)));
  return ABISP();
}

Status ABISysV_rx::SetReturnValueObject(StackFrameSP &frame_sp,
                                        ValueObjectSP &new_value_sp) {
  return Status::FromErrorString("setting a return value is not supported");
}

// Scalars of up to 8 bytes (integers, pointers and the soft-float float and
// double) come back in r1, with the upper half of a 64-bit value in r2.
// Larger values are returned through memory.
ValueObjectSP
ABISysV_rx::GetReturnValueObjectImpl(Thread &thread,
                                     CompilerType &return_compiler_type) const {
  if (!return_compiler_type)
    return ValueObjectSP();
  RegisterContextSP reg_ctx = thread.GetRegisterContext();
  if (!reg_ctx)
    return ValueObjectSP();

  const uint32_t type_flags = return_compiler_type.GetTypeInfo();
  const bool is_float = return_compiler_type.IsRealFloatingPointType();
  if (!(type_flags & (eTypeIsInteger | eTypeIsPointer | eTypeIsEnumeration)) &&
      !is_float)
    return ValueObjectSP();
  const uint64_t byte_size =
      llvm::expectedToOptional(return_compiler_type.GetByteSize(&thread))
          .value_or(0);
  if (byte_size == 0 || byte_size > 8)
    return ValueObjectSP();

  const RegisterInfo *r1 = reg_ctx->GetRegisterInfoByName("r1");
  const RegisterInfo *r2 = reg_ctx->GetRegisterInfoByName("r2");
  if (!r1 || (byte_size > 4 && !r2))
    return ValueObjectSP();
  uint64_t raw = reg_ctx->ReadRegisterAsUnsigned(r1, 0) & 0xffffffff;
  if (byte_size > 4)
    raw |= reg_ctx->ReadRegisterAsUnsigned(r2, 0) << 32;

  Value value;
  value.SetCompilerType(return_compiler_type);
  value.SetValueType(Value::ValueType::Scalar);
  Scalar &scalar = value.GetScalar();
  if (is_float) {
    if (byte_size == 4) {
      uint32_t bits = raw;
      float f;
      memcpy(&f, &bits, sizeof f);
      scalar = f;
    } else if (byte_size == 8) {
      double d;
      memcpy(&d, &raw, sizeof d);
      scalar = d;
    } else {
      return ValueObjectSP();
    }
  } else {
    const bool is_signed = (type_flags & eTypeIsSigned) != 0;
    llvm::APInt bits(byte_size * 8, raw & (byte_size == 8
                                               ? UINT64_MAX
                                               : (1ULL << (byte_size * 8)) - 1));
    scalar = Scalar(llvm::APSInt(bits, !is_signed));
  }
  return ValueObjectConstResult::Create(thread.GetStackFrameAtIndex(0).get(),
                                        value, ConstString(""));
}

// At the first instruction of a function the return address that the call
// pushed is at [r0]: CFA = r0 + 4, pc at CFA - 4.
UnwindPlanSP ABISysV_rx::CreateFunctionEntryUnwindPlan() {
  UnwindPlan::Row row;
  row.GetCFAValue().SetIsRegisterPlusOffset(dwarf_r0, 4);
  row.SetRegisterLocationToAtCFAPlusOffset(dwarf_pc, -4, true);
  row.SetRegisterLocationToIsCFAPlusOffset(dwarf_r0, 0, true);

  auto plan_sp = std::make_shared<UnwindPlan>(eRegisterKindDWARF);
  plan_sp->AppendRow(std::move(row));
  plan_sp->SetSourceName("rx at-func-entry default");
  plan_sp->SetSourcedFromCompiler(eLazyBoolNo);
  return plan_sp;
}

// Without CFI, assume the function-entry layout; the compiler's CFI
// (.debug_frame / .eh_frame) describes the real frames.
UnwindPlanSP ABISysV_rx::CreateDefaultUnwindPlan() {
  UnwindPlan::Row row;
  row.GetCFAValue().SetIsRegisterPlusOffset(dwarf_r0, 4);
  row.SetRegisterLocationToAtCFAPlusOffset(dwarf_pc, -4, true);
  row.SetRegisterLocationToIsCFAPlusOffset(dwarf_r0, 0, true);
  row.SetUnspecifiedRegistersAreUndefined(true);

  auto plan_sp = std::make_shared<UnwindPlan>(eRegisterKindDWARF);
  plan_sp->AppendRow(std::move(row));
  plan_sp->SetSourceName("rx default unwind plan");
  plan_sp->SetSourcedFromCompiler(eLazyBoolNo);
  plan_sp->SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);
  return plan_sp;
}

// r6-r13 are callee-saved (and r0, the stack pointer, is restored); the
// other registers, the flags, the accumulators and the DFPU registers are
// not preserved across calls.
bool ABISysV_rx::RegisterIsVolatile(const RegisterInfo *reg_info) {
  if (!reg_info)
    return true;
  const uint32_t dwarf = reg_info->kinds[eRegisterKindDWARF];
  return !(dwarf == dwarf_r0 || (dwarf >= dwarf_r6 && dwarf <= dwarf_r13));
}

void ABISysV_rx::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                "System V ABI for RX targets", CreateInstance);
}

void ABISysV_rx::Terminate() { PluginManager::UnregisterPlugin(CreateInstance); }
