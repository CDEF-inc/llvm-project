//===- RXELFStreamer.cpp - RX ELF Target Streamer Methods -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Sets the ELF header flags the way GNU as (rx-elf) does, so that GNU ld
// accepts mixing LLVM and GCC objects.
//
//===----------------------------------------------------------------------===//

#include "RXMCTargetDesc.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"

using namespace llvm;

namespace {

// e_flags bits (binutils include/elf/rx.h).
enum : unsigned {
  E_FLAG_RX_64BIT_DOUBLES = 1 << 0,
  // Stacked arguments use natural alignment (the default RX ABI, as opposed
  // to -mgcc-abi).
  E_FLAG_RX_ABI = 1 << 3,
  E_FLAG_RX_V2 = 1 << 8,
  E_FLAG_RX_V3 = 1 << 9,
};

class RXTargetELFStreamer : public MCTargetStreamer {
public:
  RXTargetELFStreamer(MCStreamer &S, const MCSubtargetInfo &STI)
      : MCTargetStreamer(S) {
    unsigned Flags = E_FLAG_RX_ABI;
    if (STI.hasFeature(RX::Feature64BitDoubles))
      Flags |= E_FLAG_RX_64BIT_DOUBLES;
    if (STI.hasFeature(RX::FeatureRXv3))
      Flags |= E_FLAG_RX_V3;
    else if (STI.hasFeature(RX::FeatureRXv2))
      Flags |= E_FLAG_RX_V2;
    ELFObjectWriter &W = static_cast<MCELFStreamer &>(S).getWriter();
    W.setELFHeaderEFlags(W.getELFHeaderEFlags() | Flags);
  }
};

} // end anonymous namespace

MCTargetStreamer *llvm::createRXObjectTargetStreamer(MCStreamer &S,
                                                     const MCSubtargetInfo &STI) {
  if (STI.getTargetTriple().isOSBinFormatELF())
    return new RXTargetELFStreamer(S, STI);
  return nullptr;
}
