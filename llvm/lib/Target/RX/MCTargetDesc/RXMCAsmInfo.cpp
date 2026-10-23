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
  // GNU as for rx-elf rejects .cfi_* directives (GCC does not emit them
  // either), so there is no unwind information for now.
  ExceptionsType = ExceptionHandling::None;
  DwarfRegNumForCFI = true;
  UsesELFSectionDirectiveForBSS = true;
}
