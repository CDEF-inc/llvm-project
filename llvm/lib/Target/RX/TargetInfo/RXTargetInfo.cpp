//===- RXTargetInfo.cpp - RX Target Implementation -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TargetInfo/RXTargetInfo.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

Target &llvm::getTheRXTarget() {
  static Target TheRXTarget;
  return TheRXTarget;
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeRXTargetInfo() {
  RegisterTarget<Triple::rx> X(getTheRXTarget(), "rx", "Renesas RX", "RX");
}
