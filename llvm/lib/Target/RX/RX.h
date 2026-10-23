//===- RX.h - Top-level interface for RX representation ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in the
// LLVM RX back-end.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RX_RX_H
#define LLVM_LIB_TARGET_RX_RX_H

#include "MCTargetDesc/RXMCTargetDesc.h"
#include "llvm/Support/CodeGen.h"

namespace llvm {

class FunctionPass;
class PassRegistry;
class RXTargetMachine;

namespace RXCC {
// Condition codes of BCnd / SCCnd (the 4-bit cond field). A condition and
// its inverse differ only in bit 0.
enum CondCode {
  EQ = 0x0,
  NE = 0x1,
  GEU = 0x2,
  LTU = 0x3,
  GTU = 0x4,
  LEU = 0x5,
  PZ = 0x6,
  N = 0x7,
  GE = 0x8,
  LT = 0x9,
  GT = 0xa,
  LE = 0xb,
  O = 0xc,
  NO = 0xd,
};

inline CondCode getInverse(CondCode CC) {
  return static_cast<CondCode>(CC ^ 1);
}
} // end namespace RXCC

FunctionPass *createRXISelDag(RXTargetMachine &TM, CodeGenOptLevel OptLevel);

void initializeRXAsmPrinterPass(PassRegistry &);
void initializeRXDAGToDAGISelLegacyPass(PassRegistry &);

} // end namespace llvm

#endif // LLVM_LIB_TARGET_RX_RX_H
