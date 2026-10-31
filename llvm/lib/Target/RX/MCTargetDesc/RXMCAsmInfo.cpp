//===- RXMCAsmInfo.cpp - RX asm properties ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RXMCAsmInfo.h"

using namespace llvm;

void RXMCAsmInfo::anchor() {}

RXMCAsmInfo::RXMCAsmInfo(const Triple &TT, const MCTargetOptions &Options)
    : MCAsmInfoELF(Options) {
  CodePointerSize = 4;
  CalleeSaveStackSlotSize = 4;
  // GNU as for rx-elf uses ';' as the line comment character.
  CommentString = ";";
  SupportsDebugInformation = true;
  // There are no exceptions (panic=abort), but functions with debug info get
  // a .debug_frame, and ones with uwtable an .eh_frame. Both come from
  // .cfi_* directives, which GNU as for rx-elf rejects (GCC writes the
  // .debug_frame bytes itself), so only the integrated assembler can
  // assemble such output.
  ExceptionsType = ExceptionHandling::None;
  UsesCFIWithoutEH = true;
  DwarfRegNumForCFI = true;
  UsesELFSectionDirectiveForBSS = true;
}
