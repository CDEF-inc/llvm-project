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
#include "llvm/CodeGen/MachineRegisterInfo.h"
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

  // Skip the callee-saved push.
  while (MBBI != MBB.end() && MBBI->getFlag(MachineInstr::FrameSetup) &&
         (MBBI->getOpcode() == RX::PUSH_L || MBBI->getOpcode() == RX::PUSHM))
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

  MachineBasicBlock::iterator Term = MBB.getFirstTerminator();
  DebugLoc DL = Term != MBB.end() ? Term->getDebugLoc() : DebugLoc();

  // The callee-saved pop (pop or popm) inserted by
  // restoreCalleeSavedRegisters, if any, is right before the terminator.
  MachineBasicBlock::iterator MBBI = Term;
  MachineInstr *Restore = nullptr;
  if (MBBI != MBB.begin()) {
    MachineInstr &PI = *std::prev(MBBI);
    if ((PI.getOpcode() == RX::POP || PI.getOpcode() == RX::POPM) &&
        PI.getFlag(MachineInstr::FrameDestroy)) {
      Restore = &PI;
      MBBI = PI.getIterator();
    }
  }

  uint64_t NumBytes = MFI.getStackSize() - FI->getCalleeSavedFrameSize();

  // rtsd #imm, lo-hi frees imm bytes, the registers included, then pops
  // lo..hi and returns; rtsd #imm frees imm bytes and returns. imm is at
  // most 1020.
  if (Term != MBB.end() && Term->getOpcode() == RX::RTS) {
    uint64_t RegBytes = FI->getCalleeSavedFrameSize();
    if (!Restore && NumBytes == 0)
      return;
    if (NumBytes + RegBytes > 1020) {
      adjustSP(MBB, MBBI, DL, TII, static_cast<int64_t>(NumBytes),
               MachineInstr::FrameDestroy);
      NumBytes = 0;
      if (!Restore)
        return;
    }
    MachineInstrBuilder MIB;
    if (Restore) {
      Register Lo, Hi;
      if (Restore->getOpcode() == RX::POP) {
        Lo = Hi = Restore->getOperand(0).getReg();
      } else {
        Lo = Restore->getOperand(0).getReg();
        Hi = Restore->getOperand(1).getReg();
      }
      MIB = BuildMI(MBB, Term, DL, TII.get(RX::RTSD_irr))
                .addImm(NumBytes + RegBytes)
                .addReg(Lo, RegState::Undef)
                .addReg(Hi, RegState::Undef);
      Restore->eraseFromParent();
    } else {
      MIB = BuildMI(MBB, Term, DL, TII.get(RX::RTSD_i)).addImm(NumBytes);
    }
    // The return value registers.
    MIB.copyImplicitOps(*Term).setMIFlag(MachineInstr::FrameDestroy);
    Term->eraseFromParent();
    return;
  }

  if (NumBytes)
    adjustSP(MBB, MBBI, DL, TII, static_cast<int64_t>(NumBytes),
             MachineInstr::FrameDestroy);
}

void RXFrameLowering::determineCalleeSaves(MachineFunction &MF,
                                           BitVector &SavedRegs,
                                           RegScavenger *RS) const {
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);

  // Save one range of registers: the callee-saved registers are R6..R13
  // in order, so fill in the ones between the lowest and the highest used.
  const MCPhysReg *CSRegs = MF.getRegInfo().getCalleeSavedRegs();
  int First = -1, Last = -1;
  for (int I = 0; CSRegs[I]; ++I) {
    if (!SavedRegs.test(CSRegs[I]))
      continue;
    if (First < 0)
      First = I;
    Last = I;
  }
  for (int I = First; I >= 0 && I <= Last; ++I)
    SavedRegs.set(CSRegs[I]);
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

  for (const CalleeSavedInfo &I : CSI)
    MBB.addLiveIn(I.getReg());

  // One register: push.l (GNU as also turns pushm r6-r6 into push.l r6).
  MCRegister Lo = CSI.front().getReg(), Hi = CSI.back().getReg();
  assert(TRI->getEncodingValue(Hi) - TRI->getEncodingValue(Lo) + 1 ==
             CSI.size() &&
         "callee-saved registers are not one range");
  if (CSI.size() == 1) {
    BuildMI(MBB, MI, DL, TII.get(RX::PUSH_L))
        .addReg(Lo, RegState::Kill)
        .setMIFlag(MachineInstr::FrameSetup);
    return true;
  }
  MachineInstrBuilder MIB = BuildMI(MBB, MI, DL, TII.get(RX::PUSHM))
                                .addReg(Lo, RegState::Kill)
                                .addReg(Hi, RegState::Kill)
                                .setMIFlag(MachineInstr::FrameSetup);
  for (const CalleeSavedInfo &I : CSI.drop_front().drop_back())
    MIB.addReg(I.getReg(), RegState::Implicit | RegState::Kill);
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

  // emitEpilogue folds this into rtsd before an rts.
  const TargetInstrInfo &TII = *STI.getInstrInfo();
  MCRegister Lo = CSI.front().getReg(), Hi = CSI.back().getReg();
  if (CSI.size() == 1) {
    BuildMI(MBB, MI, DL, TII.get(RX::POP), Lo)
        .setMIFlag(MachineInstr::FrameDestroy);
    return true;
  }
  // popm takes the range as operands; the registers it loads are implicit
  // definitions.
  MachineInstrBuilder MIB = BuildMI(MBB, MI, DL, TII.get(RX::POPM))
                                .addReg(Lo, RegState::Undef)
                                .addReg(Hi, RegState::Undef)
                                .setMIFlag(MachineInstr::FrameDestroy);
  for (const CalleeSavedInfo &I : CSI)
    MIB.addReg(I.getReg(), RegState::ImplicitDefine);
  return true;
}

MachineBasicBlock::iterator RXFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  // The outgoing argument area is reserved in the prologue.
  return MBB.erase(I);
}
