//===- RXFrameLowering.h - Define frame lowering for RX ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Stack frame layout (the stack grows down; SP is R0):
//
//   [caller's outgoing arguments]   <- SP at entry + 4
//   return address                  <- SP at entry (pushed by BSR/JSR)
//   callee-saved registers          (push.l each)
//   locals and spill slots
//   outgoing arguments              <- SP after the prologue
//
// No frame pointer is used.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RX_RXFRAMELOWERING_H
#define LLVM_LIB_TARGET_RX_RXFRAMELOWERING_H

#include "llvm/CodeGen/TargetFrameLowering.h"

namespace llvm {

class RXSubtarget;

class RXFrameLowering : public TargetFrameLowering {
  const RXSubtarget &STI;

protected:
  bool hasFPImpl(const MachineFunction &MF) const override;

public:
  explicit RXFrameLowering(const RXSubtarget &STI);

  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;

  MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator I) const override;

  bool spillCalleeSavedRegisters(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MI,
                                 ArrayRef<CalleeSavedInfo> CSI,
                                 const TargetRegisterInfo *TRI) const override;
  bool
  restoreCalleeSavedRegisters(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MI,
                              MutableArrayRef<CalleeSavedInfo> CSI,
                              const TargetRegisterInfo *TRI) const override;

  bool hasReservedCallFrame(const MachineFunction &MF) const override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_RX_RXFRAMELOWERING_H
