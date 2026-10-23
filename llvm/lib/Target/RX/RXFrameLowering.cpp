//===- RXFrameLowering.cpp - RX Frame Information ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the RX implementation of TargetFrameLowering.
//
//===----------------------------------------------------------------------===//

#include "RXFrameLowering.h"
#include "RX.h"
#include "RXInstrInfo.h"
#include "RXMachineFunctionInfo.h"
#include "RXSubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

RXFrameLowering::RXFrameLowering(const RXSubtarget &STI)
    : TargetFrameLowering(TargetFrameLowering::StackGrowsDown, Align(4),
                          /*LocalAreaOffset=*/-4, Align(4)),
      STI(STI) {}

bool RXFrameLowering::hasFPImpl(const MachineFunction &MF) const {
  return false;
}

bool RXFrameLowering::hasReservedCallFrame(const MachineFunction &MF) const {
  return !MF.getFrameInfo().hasVarSizedObjects();
}

// SP += Amount (a negative Amount allocates).
static void adjustSP(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                     const DebugLoc &DL, const TargetInstrInfo &TII,
                     int64_t Amount, MachineInstr::MIFlag Flag) {
  MachineInstr *MI = BuildMI(MBB, MBBI, DL, TII.get(RX::ADD_rri32), RX::R0)
                         .addReg(RX::R0)
                         .addImm(Amount)
                         .setMIFlag(Flag);
  MI->findRegisterDefOperand(RX::PSW, /*TRI=*/nullptr)->setIsDead();
}

void RXFrameLowering::emitPrologue(MachineFunction &MF,
                                   MachineBasicBlock &MBB) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  if (MFI.hasVarSizedObjects())
    report_fatal_error("RX: dynamic stack allocation is not supported yet");

  const TargetInstrInfo &TII = *STI.getInstrInfo();
  auto *FI = MF.getInfo<RXMachineFunctionInfo>();
  MachineBasicBlock::iterator MBBI = MBB.begin();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();

  // Skip the callee-saved pushes.
  while (MBBI != MBB.end() && MBBI->getFlag(MachineInstr::FrameSetup) &&
         MBBI->getOpcode() == RX::PUSH_L)
    ++MBBI;

  uint64_t NumBytes = MFI.getStackSize() - FI->getCalleeSavedFrameSize();
  if (NumBytes)
    adjustSP(MBB, MBBI, DL, TII, -static_cast<int64_t>(NumBytes),
             MachineInstr::FrameSetup);
}

void RXFrameLowering::emitEpilogue(MachineFunction &MF,
                                   MachineBasicBlock &MBB) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const TargetInstrInfo &TII = *STI.getInstrInfo();
  auto *FI = MF.getInfo<RXMachineFunctionInfo>();

  MachineBasicBlock::iterator MBBI = MBB.getFirstTerminator();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();

  // Insert before the callee-saved pops.
  while (MBBI != MBB.begin()) {
    MachineBasicBlock::iterator PI = std::prev(MBBI);
    if (PI->getOpcode() != RX::POP || !PI->getFlag(MachineInstr::FrameDestroy))
      break;
    MBBI = PI;
  }

  uint64_t NumBytes = MFI.getStackSize() - FI->getCalleeSavedFrameSize();
  if (NumBytes)
    adjustSP(MBB, MBBI, DL, TII, static_cast<int64_t>(NumBytes),
             MachineInstr::FrameDestroy);
}

bool RXFrameLowering::spillCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    ArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  if (CSI.empty())
    return false;

  DebugLoc DL;
  if (MI != MBB.end())
    DL = MI->getDebugLoc();

  MachineFunction &MF = *MBB.getParent();
  const TargetInstrInfo &TII = *STI.getInstrInfo();
  MF.getInfo<RXMachineFunctionInfo>()->setCalleeSavedFrameSize(CSI.size() * 4);

  for (const CalleeSavedInfo &I : CSI) {
    MCRegister Reg = I.getReg();
    MBB.addLiveIn(Reg);
    BuildMI(MBB, MI, DL, TII.get(RX::PUSH_L))
        .addReg(Reg, RegState::Kill)
        .setMIFlag(MachineInstr::FrameSetup);
  }
  return true;
}

bool RXFrameLowering::restoreCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    MutableArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  if (CSI.empty())
    return false;

  DebugLoc DL;
  if (MI != MBB.end())
    DL = MI->getDebugLoc();

  const TargetInstrInfo &TII = *STI.getInstrInfo();
  for (const CalleeSavedInfo &I : llvm::reverse(CSI))
    BuildMI(MBB, MI, DL, TII.get(RX::POP), I.getReg())
        .setMIFlag(MachineInstr::FrameDestroy);
  return true;
}

MachineBasicBlock::iterator RXFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  // The outgoing argument area is reserved in the prologue.
  return MBB.erase(I);
}
