//===- RXSubtarget.cpp - RX Subtarget Information ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the RX specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#include "RXSubtarget.h"
#include "RX.h"
#include "RXSelectionDAGInfo.h"

using namespace llvm;

#define DEBUG_TYPE "rx-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "RXGenSubtargetInfo.inc"

void RXSubtarget::anchor() {}

RXSubtarget &RXSubtarget::initializeSubtargetDependencies(StringRef CPU,
                                                          StringRef FS) {
  StringRef CPUName = CPU.empty() ? "rxv3" : CPU;
  ParseSubtargetFeatures(CPUName, /*TuneCPU=*/CPUName, FS);
  return *this;
}

RXSubtarget::RXSubtarget(const Triple &TT, const std::string &CPU,
                         const std::string &FS, const TargetMachine &TM)
    : RXGenSubtargetInfo(TT, CPU, /*TuneCPU=*/CPU, FS),
      InstrInfo(initializeSubtargetDependencies(CPU, FS)), FrameLowering(*this),
      TLInfo(TM, *this), TSInfo(std::make_unique<RXSelectionDAGInfo>()) {}

RXSubtarget::~RXSubtarget() = default;

const SelectionDAGTargetInfo *RXSubtarget::getSelectionDAGInfo() const {
  return TSInfo.get();
}
