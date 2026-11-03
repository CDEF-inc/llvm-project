//===- RXELFObjectWriter.cpp - RX ELF Writer --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/RXFixupKinds.h"
#include "MCTargetDesc/RXMCTargetDesc.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCValue.h"

using namespace llvm;

namespace {

class RXELFObjectWriter : public MCELFObjectTargetWriter {
public:
  RXELFObjectWriter(uint8_t OSABI, bool Relax)
      : MCELFObjectTargetWriter(/*Is64Bit=*/false, OSABI, ELF::EM_RX,
                                /*HasRelocationAddend=*/true),
        Relax(Relax) {}

  // With linker relaxation, relocations name the symbol (also a local
  // label), not section + offset: the linker moves the symbols when it
  // deletes bytes.
  bool needsRelocateWithSymbol(const MCValue &, unsigned Type) const override {
    return Relax;
  }

protected:
  unsigned getRelocType(const MCFixup &Fixup, const MCValue &Target,
                        bool IsPCRel) const override {
    switch (Fixup.getKind()) {
    case FK_Data_1:
      return IsPCRel ? ELF::R_RX_DIR8S_PCREL : ELF::R_RX_DIR8;
    case FK_Data_2:
      return IsPCRel ? ELF::R_RX_DIR16S_PCREL : ELF::R_RX_DIR16;
    case FK_Data_4:
      return ELF::R_RX_DIR32;
    case RX::fixup_rx_pcrel3:
      return ELF::R_RX_DIR3U_PCREL;
    case RX::fixup_rx_pcrel8:
      return ELF::R_RX_DIR8S_PCREL;
    case RX::fixup_rx_pcrel16:
      return ELF::R_RX_DIR16S_PCREL;
    case RX::fixup_rx_pcrel24:
      return ELF::R_RX_DIR24S_PCREL;
    }
    reportError(Fixup.getLoc(), "unsupported relocation");
    return ELF::R_RX_NONE;
  }

private:
  bool Relax;
};

} // end anonymous namespace

std::unique_ptr<MCObjectTargetWriter>
llvm::createRXELFObjectWriter(uint8_t OSABI, bool Relax) {
  return std::make_unique<RXELFObjectWriter>(OSABI, Relax);
}
