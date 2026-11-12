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
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"

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
      TLInfo(TM, *this), TSInfo(std::make_unique<RXSelectionDAGInfo>()) {
  // The calling convention: CC-RX's (the default) or GNURX's.
  StringRef ABI = TM.Options.MCOptions.getABIName();
  if (ABI == "gnu")
    GNUABI = true;
  else if (!ABI.empty() && ABI != "ccrx")
    report_fatal_error("RX: unknown target ABI '" + ABI +
                       "' (expected ccrx or gnu)");
}

RXSubtarget::~RXSubtarget() = default;

const SelectionDAGTargetInfo *RXSubtarget::getSelectionDAGInfo() const {
  return TSInfo.get();
}
