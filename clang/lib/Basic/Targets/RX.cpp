//===--- RX.cpp - Implement RX target feature support ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the RX TargetInfo.
//
//===----------------------------------------------------------------------===//

#include "RX.h"
#include "clang/Basic/MacroBuilder.h"

using namespace clang;
using namespace clang::targets;

const char *const RXTargetInfo::GCCRegNames[] = {
    "r0", "r1", "r2",  "r3",  "r4",  "r5",  "r6",  "r7",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"};

ArrayRef<const char *> RXTargetInfo::getGCCRegNames() const {
  return llvm::ArrayRef(GCCRegNames);
}

void RXTargetInfo::getTargetDefines(const LangOptions &Opts,
                                    MacroBuilder &Builder) const {
  // As GCC defines them for rx-elf -m64bit-doubles.
  Builder.defineMacro("__RX__");
  Builder.defineMacro("__RX_LITTLE_ENDIAN__");
  Builder.defineMacro("__RX_64BIT_DOUBLES__");
  Builder.defineMacro("__RX_GAS_SYNTAX__");
  Builder.defineMacro("__RX_GCC_ABI__");
}
