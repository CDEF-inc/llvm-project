//===- RXFixupKinds.h - RX Specific Fixup Entries ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RX_MCTARGETDESC_RXFIXUPKINDS_H
#define LLVM_LIB_TARGET_RX_MCTARGETDESC_RXFIXUPKINDS_H

#include "llvm/MC/MCFixup.h"

namespace llvm {
namespace RX {

// PC-relative branch displacements are measured from the first byte of the
// instruction. The 8/16/24-bit fields start one byte into the instruction;
// the 3-bit field is the low bits of the opcode byte itself.
//
// This table must be in the same order as the Infos array in
// RXAsmBackend.cpp.
enum Fixups {
  // bra.s / beq.s / bne.s: 3-bit displacement 3..10 (R_RX_DIR3U_PCREL).
  fixup_rx_pcrel3 = FirstTargetFixupKind,
  // 8-bit signed displacement at offset 1 (R_RX_DIR8S_PCREL).
  fixup_rx_pcrel8,
  // 16-bit signed little-endian displacement at offset 1 (R_RX_DIR16S_PCREL).
  fixup_rx_pcrel16,
  // 24-bit signed little-endian displacement at offset 1 (R_RX_DIR24S_PCREL).
  fixup_rx_pcrel24,

  LastTargetFixupKind,
  NumTargetFixupKinds = LastTargetFixupKind - FirstTargetFixupKind
};

} // end namespace RX
} // end namespace llvm

#endif // LLVM_LIB_TARGET_RX_MCTARGETDESC_RXFIXUPKINDS_H
