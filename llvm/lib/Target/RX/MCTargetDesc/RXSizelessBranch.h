//===- RXSizelessBranch.h - Branches written without a size -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// GNU as accepts "bra label", "bsr label" and "b<cond> label" without a size
// and picks the form from the distance. The parser emits these as the
// BR_SIZELESS pseudo (operands: target, kind, step); RXAsmBackend relaxes the
// step and RXMCCodeEmitter expands the pseudo into the instructions below.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RX_MCTARGETDESC_RXSIZELESSBRANCH_H
#define LLVM_LIB_TARGET_RX_MCTARGETDESC_RXSIZELESSBRANCH_H

#include "llvm/ADT/ArrayRef.h"

namespace llvm {
namespace RX {

// The kind operand: bra, bsr, or b<cond> with the BCnd condition code.
enum : unsigned { BrKindBra = 0, BrKindBsr = 1, BrKindCond = 2 };

// The forms, smallest first. The Inv* forms branch around a bra on the
// inverted condition, as GNU as does when b<cond> cannot reach.
enum SizelessBranchForm {
  BrFormS,     // bra.s / beq.s / bne.s (displacement 3..10)
  BrFormB,     // .b (8 bits)
  BrFormW,     // .w (16 bits)
  BrFormA,     // .a (24 bits)
  BrFormInvSA, // b<!cond>.s 5; bra.a
  BrFormInvBW, // b<!cond>.b 5; bra.w
  BrFormInvBA, // b<!cond>.b 6; bra.a
};

// The forms a kind steps through. GNU as: bra .s/.b/.w/.a; bsr .w/.a;
// beq / bne .s/.b/.w and then around a bra.a; the other conditions .b and
// then around a bra.w or a bra.a.
inline ArrayRef<SizelessBranchForm> getSizelessBranchForms(unsigned Kind) {
  static const SizelessBranchForm Bra[] = {BrFormS, BrFormB, BrFormW, BrFormA};
  static const SizelessBranchForm Bsr[] = {BrFormW, BrFormA};
  static const SizelessBranchForm EqNe[] = {BrFormS, BrFormB, BrFormW,
                                            BrFormInvSA};
  static const SizelessBranchForm Other[] = {BrFormB, BrFormInvBW,
                                             BrFormInvBA};
  if (Kind == BrKindBra)
    return Bra;
  if (Kind == BrKindBsr)
    return Bsr;
  unsigned Cond = Kind - BrKindCond;
  if (Cond <= 1)
    return EqNe;
  return Other;
}

// The step GNU as uses for a target it cannot resolve (an undefined or
// global symbol, or one in another section): bra.a, bsr.a, beq.w / bne.w,
// and b<cond>.b for the other conditions.
inline unsigned getSizelessBranchUnresolvedStep(unsigned Kind) {
  if (Kind == BrKindBra)
    return 3;
  if (Kind == BrKindBsr)
    return 1;
  return Kind - BrKindCond <= 1 ? 2 : 0;
}

} // end namespace RX
} // end namespace llvm

#endif // LLVM_LIB_TARGET_RX_MCTARGETDESC_RXSIZELESSBRANCH_H
