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
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Status.h"
#include "lldb/ValueObject/ValueObjectConstResult.h"

#include "llvm/TargetParser/Triple.h"

#include <cstring>
#include <string>
#include <vector>

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

// The registers a value of SIZE bytes comes back in, as bytes: r1, r2, ...
// little-endian (a 64-bit scalar is r1:r2, an aggregate the bytes of r1-r4).
static bool ReadReturnBytes(RegisterContext &reg_ctx, size_t size,
                            std::vector<uint8_t> &bytes) {
  bytes.assign((size + 3) & ~size_t(3), 0);
  for (size_t i = 0; i < bytes.size() / 4; ++i) {
    const RegisterInfo *r =
        reg_ctx.GetRegisterInfoByName(("r" + std::to_string(i + 1)).c_str());
    if (!r)
      return false;
    uint32_t v = reg_ctx.ReadRegisterAsUnsigned(r, 0);
    memcpy(&bytes[4 * i], &v, 4);
  }
  bytes.resize(size);
  return true;
}

// Whether TYPE comes from Rust, whose own ABI returns aggregates differently
// from the C ABI. DWARF does not record a function's calling convention, but
// Rust types have paths (`lldb_demo::Point`, `core::option::Option<u32>`) or
// are tuples, and C types have neither.
static bool IsRustType(const CompilerType &type) {
  llvm::StringRef name = type.GetTypeName().GetStringRef();
  return name.contains("::") || name.starts_with("(");
}

// For a two-field aggregate of scalars (rustc's ScalarPair), the fields'
// byte offsets and sizes.
static bool GetScalarPair(Thread &thread, const CompilerType &type,
                          uint64_t off[2], uint64_t size[2]) {
  if (type.GetNumFields() != 2)
    return false;
  for (uint32_t i = 0; i < 2; ++i) {
    std::string name;
    uint64_t bit_offset = 0;
    CompilerType field =
        type.GetFieldAtIndex(i, name, &bit_offset, nullptr, nullptr);
    if (!(field.GetTypeInfo() &
          (eTypeIsScalar | eTypeIsPointer | eTypeIsEnumeration)))
      return false;
    off[i] = bit_offset / 8;
    size[i] = llvm::expectedToOptional(field.GetByteSize(&thread)).value_or(0);
    if (size[i] == 0 || size[i] > 8)
      return false;
  }
  return true;
}

// The bytes of an aggregate return value, or false if it is returned in
// memory (which cannot be recovered after the return):
// - C (GCC's rx-elf ABI): 1-16 bytes whose size is a multiple of 4 come back
//   in r1-r4; the others in memory, through a pointer passed in r15.
// - Rust's own ABI: a pair of scalars comes back as two values, the first in
//   r1 (r1:r2 for 8 bytes) and the second in the next register; other
//   aggregates of up to 8 bytes as an integer in r1(:r2); larger ones in
//   memory. A pair whose second field starts at offset 4 or 8 has the same
//   bytes either way.
static bool GetAggregateBytes(Thread &thread, RegisterContext &reg_ctx,
                              const CompilerType &type, uint64_t size,
                              std::vector<uint8_t> &bytes) {
  if (size == 0 || size > 16)
    return false;
  if (!IsRustType(type))
    return size % 4 == 0 && ReadReturnBytes(reg_ctx, size, bytes);

  uint64_t off[2], fsize[2];
  if (GetScalarPair(thread, type, off, fsize)) {
    uint64_t first_regs = (fsize[0] + 3) / 4;
    if (off[1] == first_regs * 4)
      return ReadReturnBytes(reg_ctx, size, bytes);
    // e.g. (u8, u8) or (u16, u16): one register each.
    std::vector<uint8_t> regs;
    if (!ReadReturnBytes(reg_ctx, 4 * (first_regs + (fsize[1] + 3) / 4), regs))
      return false;
    bytes.assign(size, 0);
    if (off[0] + fsize[0] > size || off[1] + fsize[1] > size)
      return false;
    memcpy(&bytes[off[0]], &regs[0], fsize[0]);
    memcpy(&bytes[off[1]], &regs[4 * first_regs], fsize[1]);
    return true;
  }
  return size <= 8 && ReadReturnBytes(reg_ctx, size, bytes);
}

Status ABISysV_rx::SetReturnValueObject(StackFrameSP &frame_sp,
                                        ValueObjectSP &new_value_sp) {
  if (!new_value_sp)
    return Status::FromErrorString("empty value object for the return value");
  CompilerType type = new_value_sp->GetCompilerType();
  if (!type)
    return Status::FromErrorString("no type for the return value");
  ThreadSP thread = frame_sp->GetThread();
  RegisterContext &reg_ctx = *thread->GetRegisterContext();

  DataExtractor data;
  Status error;
  const size_t size = new_value_sp->GetData(data, error);
  if (error.Fail())
    return Status::FromErrorStringWithFormat(
        "couldn't get the return value's data: %s", error.AsCString());

  // Scalars of up to 8 bytes, and aggregates that come back in registers
  // (see GetAggregateBytes), go to r1, r2, ... as their bytes.
  const uint32_t flags = type.GetTypeInfo();
  bool in_regs = (flags & (eTypeIsScalar | eTypeIsPointer |
                           eTypeIsEnumeration)) && size <= 8;
  if (!in_regs && type.IsAggregateType()) {
    uint64_t off[2], fsize[2];
    in_regs = IsRustType(type)
                  ? (size <= 8 && !(GetScalarPair(*thread, type, off, fsize) &&
                                    off[1] != ((fsize[0] + 3) / 4) * 4))
                  : (size >= 1 && size <= 16 && size % 4 == 0);
  }
  if (!in_regs || size == 0)
    return Status::FromErrorString(
        "only values that RX returns in registers can be set");

  std::vector<uint8_t> bytes((size + 3) & ~size_t(3), 0);
  data.CopyData(0, size, bytes.data());
  for (size_t i = 0; i < bytes.size() / 4; ++i) {
    const RegisterInfo *r =
        reg_ctx.GetRegisterInfoByName(("r" + std::to_string(i + 1)).c_str());
    uint32_t v;
    memcpy(&v, &bytes[4 * i], 4);
    if (!r || !reg_ctx.WriteRegisterFromUnsigned(r, v))
      return Status::FromErrorString("couldn't write the return registers");
  }
  return Status();
}

// Integer and pointer arguments, read at the first instruction of the
// function (GCC's rx-elf ABI): r1-r4 hold the first 16 bytes, each argument
// rounded up to 4 bytes and placed only if it fits entirely; the others are
// on the stack after the return address, at [r0 + 4], 4-byte aligned.
bool ABISysV_rx::GetArgumentValues(Thread &thread, ValueList &values) const {
  RegisterContextSP reg_ctx = thread.GetRegisterContext();
  if (!reg_ctx)
    return false;
  ProcessSP process = thread.GetProcess();
  if (!process)
    return false;
  const RegisterInfo *sp = reg_ctx->GetRegisterInfoByName("r0");
  if (!sp)
    return false;
  addr_t stack = reg_ctx->ReadRegisterAsUnsigned(sp, 0) + 4;
  uint32_t used = 0; // bytes of r1-r4 taken

  for (size_t i = 0; i < values.GetSize(); ++i) {
    Value *value = values.GetValueAtIndex(i);
    if (!value)
      return false;
    CompilerType type = value->GetCompilerType();
    if (!type)
      return false;
    bool is_signed = false;
    if (!type.IsIntegerOrEnumerationType(is_signed) && !type.IsPointerType())
      return false;
    const uint64_t size =
        llvm::expectedToOptional(type.GetByteSize(&thread)).value_or(0);
    if (size == 0 || size > 8)
      return false;
    const uint32_t slots = (size + 3) / 4;

    uint64_t raw = 0;
    if (used + 4 * slots <= 16) {
      for (uint32_t k = 0; k < slots; ++k) {
        const RegisterInfo *r = reg_ctx->GetRegisterInfoByName(
            ("r" + std::to_string(used / 4 + k + 1)).c_str());
        if (!r)
          return false;
        raw |= (reg_ctx->ReadRegisterAsUnsigned(r, 0) & 0xffffffff) << (32 * k);
      }
    } else {
      Status error;
      if (process->ReadMemory(stack, &raw, size, error) != size)
        return false;
      stack += 4 * slots;
    }
    // The count advances for every argument, also one on the stack.
    used += 4 * slots;
    if (size < 8)
      raw &= (1ULL << (size * 8)) - 1;
    value->SetValueType(Value::ValueType::Scalar);
    value->GetScalar() =
        Scalar(llvm::APSInt(llvm::APInt(size * 8, raw), !is_signed));
  }
  return true;
}

// Scalars of up to 8 bytes (integers, pointers and the soft-float float and
// double) come back in r1, with the upper half of a 64-bit value in r2;
// aggregates as GetAggregateBytes says.
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
  const uint64_t byte_size =
      llvm::expectedToOptional(return_compiler_type.GetByteSize(&thread))
          .value_or(0);
  if (byte_size == 0)
    return ValueObjectSP();

  Value value;
  value.SetCompilerType(return_compiler_type);

  if (return_compiler_type.IsAggregateType()) {
    std::vector<uint8_t> bytes;
    if (!GetAggregateBytes(thread, *reg_ctx, return_compiler_type, byte_size,
                           bytes))
      return ValueObjectSP();
    value.SetBytes(bytes.data(), bytes.size());
    return ValueObjectConstResult::Create(thread.GetStackFrameAtIndex(0).get(),
                                          value, ConstString(""));
  }

  if (!(type_flags & (eTypeIsInteger | eTypeIsPointer | eTypeIsEnumeration)) &&
      !is_float)
    return ValueObjectSP();
  if (byte_size > 8)
    return ValueObjectSP();

  std::vector<uint8_t> bytes;
  if (!ReadReturnBytes(*reg_ctx, byte_size, bytes))
    return ValueObjectSP();
  uint64_t raw = 0;
  memcpy(&raw, bytes.data(), byte_size);

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
    llvm::APInt bits(byte_size * 8, raw);
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
