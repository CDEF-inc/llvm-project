//===- RXMCAsmInfo.h - RX asm properties ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the RXMCAsmInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RX_MCTARGETDESC_RXMCASMINFO_H
#define LLVM_LIB_TARGET_RX_MCTARGETDESC_RXMCASMINFO_H

#include "llvm/MC/MCAsmInfoELF.h"

namespace llvm {

class Triple;

class RXMCAsmInfo : public MCAsmInfoELF {
  void anchor() override;

public:
  explicit RXMCAsmInfo(const Triple &TT, const MCTargetOptions &Options);
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_RX_MCTARGETDESC_RXMCASMINFO_H
