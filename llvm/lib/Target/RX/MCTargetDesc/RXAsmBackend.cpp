//===- RXAsmBackend.cpp - RX Assembler Backend ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/RXFixupKinds.h"
#include "MCTargetDesc/RXMCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

namespace {

class RXAsmBackend : public MCAsmBackend {
public:
  RXAsmBackend() : MCAsmBackend(llvm::endianness::little) {}

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createRXELFObjectWriter(ELF::ELFOSABI_NONE);
  }

  MCFixupKindInfo getFixupKindInfo(MCFixupKind Kind) const override {
    // clang-format off
    static const MCFixupKindInfo Infos[RX::NumTargetFixupKinds] = {
      // name                offset bits flags
      {"fixup_rx_pcrel3",    0,     3,   0},
      {"fixup_rx_pcrel8",    0,     8,   0},
      {"fixup_rx_pcrel16",   0,     16,  0},
      {"fixup_rx_pcrel24",   0,     24,  0},
    };
    // clang-format on
    static_assert(std::size(Infos) == RX::NumTargetFixupKinds,
                  "Not all fixup kinds added to Infos array");

    if (Kind < FirstTargetFixupKind)
      return MCAsmBackend::getFixupKindInfo(Kind);
    return Infos[Kind - FirstTargetFixupKind];
  }

  void applyFixup(const MCFragment &F, const MCFixup &Fixup,
                  const MCValue &Target, uint8_t *Data, uint64_t Value,
                  bool IsResolved) override;

  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *STI) const override {
    // NOP is the single byte 0x03.
    for (uint64_t I = 0; I != Count; ++I)
      OS << char(0x03);
    return true;
  }
};

} // end anonymous namespace

void RXAsmBackend::applyFixup(const MCFragment &F, const MCFixup &Fixup,
                              const MCValue &Target, uint8_t *Data,
                              uint64_t Value, bool IsResolved) {
  maybeAddReloc(F, Fixup, Target, Value, IsResolved);
  // Unresolved fields stay zero; the RELA addend carries the value.
  if (!IsResolved)
    return;

  MCContext &Ctx = getContext();
  unsigned Kind = Fixup.getKind();
  unsigned NumBytes;
  switch (Kind) {
  case RX::fixup_rx_pcrel3: {
    // The field is in the opcode byte, so Value is already relative to the
    // start of the instruction.
    int64_t Dsp = static_cast<int64_t>(Value);
    if (Dsp < 3 || Dsp > 10)
      Ctx.reportError(Fixup.getLoc(), "branch target out of range for .S "
                                      "(3 to 10 bytes)");
    Data[0] |= uint8_t(Dsp & 7);
    return;
  }
  case RX::fixup_rx_pcrel8:
  case RX::fixup_rx_pcrel16:
  case RX::fixup_rx_pcrel24: {
    // The field is one byte into the instruction: rebase on the opcode.
    int64_t Dsp = static_cast<int64_t>(Value) + 1;
    unsigned Bits = Kind == RX::fixup_rx_pcrel8    ? 8
                    : Kind == RX::fixup_rx_pcrel16 ? 16
                                                   : 24;
    if (!isIntN(Bits, Dsp))
      Ctx.reportError(Fixup.getLoc(), "branch target out of range");
    Value = static_cast<uint64_t>(Dsp);
    NumBytes = Bits / 8;
    break;
  }
  case FK_Data_1:
    NumBytes = 1;
    break;
  case FK_Data_2:
    NumBytes = 2;
    break;
  case FK_Data_4:
    NumBytes = 4;
    break;
  default:
    llvm_unreachable("unknown fixup kind");
  }

  for (unsigned I = 0; I != NumBytes; ++I)
    Data[I] |= uint8_t((Value >> (I * 8)) & 0xff);
}

MCAsmBackend *llvm::createRXMCAsmBackend(const Target &T,
                                         const MCSubtargetInfo &STI,
                                         const MCRegisterInfo &MRI,
                                         const MCTargetOptions &Options) {
  return new RXAsmBackend();
}
