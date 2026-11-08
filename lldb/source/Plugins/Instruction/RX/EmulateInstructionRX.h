//===-- EmulateInstructionRX.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_INSTRUCTION_RX_EMULATEINSTRUCTIONRX_H
#define LLDB_SOURCE_PLUGINS_INSTRUCTION_RX_EMULATEINSTRUCTIONRX_H

#include "lldb/Core/EmulateInstruction.h"
#include "lldb/Interpreter/OptionValue.h"
#include <optional>

namespace lldb_private {

// Emulates the Renesas RX instructions that build and tear down stack frames
// (push / pushm / pop / popm / pushc, SP and frame pointer arithmetic,
// rts / rtsd) and the branches, for UnwindAssemblyInstEmulation: it lets
// LLDB unwind code that has no CFI (hand-written assembly, objects built
// without debug info).
class EmulateInstructionRX : public EmulateInstruction {
public:
  EmulateInstructionRX(const ArchSpec &arch);

  static void Initialize();

  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "rx"; }

  static llvm::StringRef GetPluginDescriptionStatic();

  static EmulateInstruction *CreateInstance(const ArchSpec &arch,
                                            InstructionType inst_type);

  static bool
  SupportsEmulatingInstructionsOfTypeStatic(InstructionType inst_type) {
    switch (inst_type) {
    case eInstructionTypeAny:
    case eInstructionTypePrologueEpilogue:
      return true;

    case eInstructionTypePCModifying:
    case eInstructionTypeAll:
      return false;
    }
    return false;
  }

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  bool SetTargetTriple(const ArchSpec &arch) override;

  bool SupportsEmulatingInstructionsOfType(InstructionType inst_type) override {
    return SupportsEmulatingInstructionsOfTypeStatic(inst_type);
  }

  bool ReadInstruction() override;

  bool EvaluateInstruction(uint32_t evaluate_options) override;

  bool TestEmulation(Stream &out_stream, ArchSpec &arch,
                     OptionValueDictionary *test_data) override {
    return false;
  }

  std::optional<RegisterInfo> GetRegisterInfo(lldb::RegisterKind reg_kind,
                                              uint32_t reg_num) override;

  bool CreateFunctionEntryUnwind(UnwindPlan &unwind_plan) override;

private:
  bool ReadSP(uint32_t &sp);
  bool WriteSP(ContextType type, uint32_t sp);
  bool Push(uint32_t reg, uint32_t &sp);
  bool Pop(uint32_t reg, uint32_t &sp);
  bool EmulatePushm(uint8_t rs, uint8_t rd);
  bool EmulatePopm(uint8_t rs, uint8_t rd);
  bool EmulateAddToSP(uint8_t rs, uint8_t rd, int32_t imm);
  bool EmulateMov(uint8_t rs, uint8_t rd);
  bool EmulateBranch(int32_t disp);

  // The byte at `i` of the instruction (0 past its end).
  uint8_t Byte(size_t i) const;
  // A little-endian signed immediate of `n` bytes at `i`.
  int32_t Signed(size_t i, size_t n) const;

  // The bytes of the instruction being evaluated (RX instructions are 1 to 8
  // bytes long).
  uint8_t m_bytes[8] = {};
  size_t m_size = 0;

  // Whether this function has set R6 as its frame pointer: only then does
  // `mov.l r6, r0` give SP a known value.
  bool m_fp_set = false;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_INSTRUCTION_RX_EMULATEINSTRUCTIONRX_H
