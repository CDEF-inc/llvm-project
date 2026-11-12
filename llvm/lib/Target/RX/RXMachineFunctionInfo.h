//===- RXMachineFunctionInfo.h - RX machine function info -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares RX-specific per-machine-function information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RX_RXMACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_RX_RXMACHINEFUNCTIONINFO_H

#include "llvm/CodeGen/MachineFunction.h"

namespace llvm {

class RXMachineFunctionInfo : public MachineFunctionInfo {
  /// Bytes of callee-saved registers pushed in the prologue.
  unsigned CalleeSavedFrameSize = 0;

  /// Virtual register holding the incoming sret pointer (returned in R1).
  Register SRetReturnReg;

  /// Fixed stack object at the first unnamed argument of a variadic
  /// function (where va_start points).
  int VarArgsFrameIndex = 0;

  /// The function's own arguments, or those of a call it makes, would go in
  /// other places under the other calling convention (CC-RX's or GNURX's);
  /// see RXAsmPrinter's ABI note.
  bool ABIDependent = false;

public:
  RXMachineFunctionInfo() = default;
  RXMachineFunctionInfo(const Function &F, const TargetSubtargetInfo *STI) {}

  MachineFunctionInfo *
  clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
        const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
      const override {
    return DestMF.cloneInfo<RXMachineFunctionInfo>(*this);
  }

  unsigned getCalleeSavedFrameSize() const { return CalleeSavedFrameSize; }
  void setCalleeSavedFrameSize(unsigned Bytes) { CalleeSavedFrameSize = Bytes; }

  Register getSRetReturnReg() const { return SRetReturnReg; }
  void setSRetReturnReg(Register Reg) { SRetReturnReg = Reg; }

  int getVarArgsFrameIndex() const { return VarArgsFrameIndex; }
  void setVarArgsFrameIndex(int FI) { VarArgsFrameIndex = FI; }

  bool isABIDependent() const { return ABIDependent; }
  void setABIDependent() { ABIDependent = true; }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_RX_RXMACHINEFUNCTIONINFO_H
