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
#include "llvm/CodeGen/CFIInstBuilder.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

RXFrameLowering::RXFrameLowering(const RXSubtarget &STI)
    : TargetFrameLowering(TargetFrameLowering::StackGrowsDown, Align(4),
                          /*LocalAreaOffset=*/-4, Align(4)),
      STI(STI) {}

// R6 is the frame pointer, as in GCC: when requested (-frame-pointer), and
// when SP moves during the function (dynamic alloca). It is set to SP at
// the end of the prologue, the bottom of the fixed frame, so frame objects
// have the same offsets from it as from SP.
//
// A function with stack objects aligned beyond the stack alignment (4)
// realigns SP instead: R6 is SP after the callee-saved push, the prologue
// allocates the frame and rounds SP down (and #-MaxAlign, r0), and the
// epilogue puts SP back from R6. The incoming arguments are addressed off
// R6 and the locals off the aligned SP, both with the positive
// displacements RX has; with dynamic allocas too, SP moves, so R7 (the base
// pointer) keeps the aligned bottom of the frame for the locals.
bool RXFrameLowering::hasFPImpl(const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  return MF.getTarget().Options.DisableFramePointerElim(MF) ||
         MFI.hasVarSizedObjects() || MFI.isFrameAddressTaken() ||
         needsRealign(MF);
}

bool RXFrameLowering::needsRealign(const MachineFunction &MF) const {
  return MF.getSubtarget().getRegisterInfo()->hasStackRealignment(MF);
}

bool RXFrameLowering::hasBP(const MachineFunction &MF) const {
  return needsRealign(MF) && MF.getFrameInfo().hasVarSizedObjects();
}

// Offsets are relative to the CFA (SP before the call pushed the return
// address). SP after the prologue, and the frame pointer if there is one,
// sit StackSize bytes below the return address; with realignment the
// locals are off the aligned SP (or the base pointer) at the same offsets,
// as PEI aligned StackSize + 4 to MaxAlign, and the fixed objects (the
// incoming arguments) off R6, RegBytes below the return address.
StackOffset
RXFrameLowering::getFrameIndexReference(const MachineFunction &MF, int FI,
                                        Register &FrameReg) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  int64_t Offset = MFI.getObjectOffset(FI) + 4;
  if (needsRealign(MF)) {
    if (MFI.isFixedObjectIndex(FI)) {
      FrameReg = RX::R6;
      auto *RXFI = MF.getInfo<RXMachineFunctionInfo>();
      return StackOffset::getFixed(Offset + RXFI->getCalleeSavedFrameSize());
    }
    FrameReg = hasBP(MF) ? RX::R7 : RX::R0;
    return StackOffset::getFixed(Offset + MFI.getStackSize());
  }
  FrameReg = hasFP(MF) ? RX::R6 : RX::R0;
  return StackOffset::getFixed(Offset + MFI.getStackSize());
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
  const TargetInstrInfo &TII = *STI.getInstrInfo();
  auto *FI = MF.getInfo<RXMachineFunctionInfo>();
  MachineBasicBlock::iterator MBBI = MBB.begin();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();

  // Skip the callee-saved push.
  while (MBBI != MBB.end() && MBBI->getFlag(MachineInstr::FrameSetup) &&
         (MBBI->getOpcode() == RX::PUSH_L || MBBI->getOpcode() == RX::PUSHM))
    ++MBBI;

  // The CFA is SP + 4 on entry (createRXMCAsmInfo).
  bool NeedsCFI = MF.needsFrameMoves();
  CFIInstBuilder CFIBuilder(MBB, MBBI, MachineInstr::FrameSetup);
  uint64_t RegBytes = FI->getCalleeSavedFrameSize();
  if (NeedsCFI && RegBytes) {
    // The push put the lowest register at the lowest address.
    CFIBuilder.buildDefCFAOffset(4 + RegBytes);
    int64_t Offset = -static_cast<int64_t>(4 + RegBytes);
    for (const CalleeSavedInfo &I : MFI.getCalleeSavedInfo()) {
      CFIBuilder.buildOffset(I.getReg(), Offset);
      Offset += 4;
    }
  }

  uint64_t NumBytes = MFI.getStackSize() - RegBytes;
  if (hasFP(MF) && !MF.getRegInfo().isReserved(RX::R6))
    report_fatal_error("RX: the frame pointer became needed after register "
                       "allocation");
  if (needsRealign(MF)) {
    // mov.l r0, r6 / add #-N, r0 / and #-MaxAlign, r0 [/ mov.l r0, r7]
    if (hasBP(MF) && !MF.getRegInfo().isReserved(RX::R7))
      report_fatal_error("RX: the base pointer became needed after register "
                         "allocation");
    BuildMI(MBB, MBBI, DL, TII.get(RX::MOV_L_rr), RX::R6)
        .addReg(RX::R0)
        .setMIFlag(MachineInstr::FrameSetup);
    if (NeedsCFI)
      CFIBuilder.buildDefCFA(RX::R6, 4 + RegBytes);
    if (NumBytes)
      adjustSP(MBB, MBBI, DL, TII, -static_cast<int64_t>(NumBytes),
               MachineInstr::FrameSetup);
    MachineInstr *MI =
        BuildMI(MBB, MBBI, DL, TII.get(RX::AND_ri32), RX::R0)
            .addReg(RX::R0)
            .addImm(-static_cast<int64_t>(MFI.getMaxAlign().value()))
            .setMIFlag(MachineInstr::FrameSetup);
    MI->findRegisterDefOperand(RX::PSW, /*TRI=*/nullptr)->setIsDead();
    if (hasBP(MF))
      BuildMI(MBB, MBBI, DL, TII.get(RX::MOV_L_rr), RX::R7)
          .addReg(RX::R0)
          .setMIFlag(MachineInstr::FrameSetup);
    return;
  }
  if (hasFP(MF)) {
    // add #-N, r0, r6 / mov.l r6, r0 (as GCC), or mov.l r0, r6 for an
    // empty frame. From the first instruction on, the CFA is R6 + offset,
    // which stays right while SP moves.
    if (NumBytes) {
      MachineInstr *MI =
          BuildMI(MBB, MBBI, DL, TII.get(RX::ADD_rri32), RX::R6)
              .addReg(RX::R0)
              .addImm(-static_cast<int64_t>(NumBytes))
              .setMIFlag(MachineInstr::FrameSetup);
      MI->findRegisterDefOperand(RX::PSW, /*TRI=*/nullptr)->setIsDead();
    } else {
      BuildMI(MBB, MBBI, DL, TII.get(RX::MOV_L_rr), RX::R6)
          .addReg(RX::R0)
          .setMIFlag(MachineInstr::FrameSetup);
    }
    if (NeedsCFI)
      CFIBuilder.buildDefCFA(RX::R6, 4 + RegBytes + NumBytes);
    if (NumBytes)
      BuildMI(MBB, MBBI, DL, TII.get(RX::MOV_L_rr), RX::R0)
          .addReg(RX::R6)
          .setMIFlag(MachineInstr::FrameSetup);
    return;
  }
  if (NumBytes) {
    adjustSP(MBB, MBBI, DL, TII, -static_cast<int64_t>(NumBytes),
             MachineInstr::FrameSetup);
    if (NeedsCFI)
      CFIBuilder.buildDefCFAOffset(4 + RegBytes + NumBytes);
  }
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

  uint64_t RegBytes = FI->getCalleeSavedFrameSize();
  uint64_t NumBytes = MFI.getStackSize() - RegBytes;

  // The CFI follows each instruction that moves SP but does not return.
  // CFIFixup puts .cfi_remember_state / .cfi_restore_state around an
  // epilogue that is not at the end of the function. With a frame pointer
  // the CFA is R6 + offset until R6 is restored, so only the pop needs CFI.
  bool NeedsCFI = MF.needsFrameMoves();
  bool FP = hasFP(MF);

  // SP back to the bottom of the fixed frame, where R6 points; with
  // realignment, R6 points right below the callee-saved registers, so the
  // frame is freed with it.
  bool Realign = needsRealign(MF);
  if (FP && (MFI.hasVarSizedObjects() || Realign))
    BuildMI(MBB, MBBI, DL, TII.get(RX::MOV_L_rr), RX::R0)
        .addReg(RX::R6)
        .setMIFlag(MachineInstr::FrameDestroy);
  if (Realign)
    NumBytes = 0;

  // rtsd #imm, lo-hi frees imm bytes, the registers included, then pops
  // lo..hi and returns; rtsd #imm frees imm bytes and returns. imm is at
  // most 1020.
  if (Term != MBB.end() && Term->getOpcode() == RX::RTS) {
    if (!Restore && NumBytes == 0)
      return;
    if (NumBytes + RegBytes > 1020) {
      adjustSP(MBB, MBBI, DL, TII, static_cast<int64_t>(NumBytes),
               MachineInstr::FrameDestroy);
      if (NeedsCFI && !FP)
        CFIInstBuilder(MBB, MBBI, MachineInstr::FrameDestroy)
            .buildDefCFAOffset(4 + RegBytes);
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
  if (NeedsCFI && FP)
    CFIInstBuilder(MBB, MBBI, MachineInstr::FrameDestroy)
        .buildDefCFA(RX::R0, 4 + RegBytes);
  else if (NeedsCFI && NumBytes)
    CFIInstBuilder(MBB, MBBI, MachineInstr::FrameDestroy)
        .buildDefCFAOffset(4 + RegBytes);
  if (Restore && NeedsCFI) {
    CFIInstBuilder CFIBuilder(MBB, Term, MachineInstr::FrameDestroy);
    CFIBuilder.buildDefCFAOffset(4);
    for (const CalleeSavedInfo &I : MFI.getCalleeSavedInfo())
      CFIBuilder.buildRestore(I.getReg());
  }
}

// A frame with offsets beyond dsp:16 for byte accesses (64 KB) may need a
// scratch register for an access (RXRegisterInfo::eliminateFrameIndex):
// give the scavenger a slot to spill one to when none is free. PEI puts
// it next to SP, in reach of any access.
void RXFrameLowering::processFunctionBeforeFrameFinalized(
    MachineFunction &MF, RegScavenger *RS) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  if (RS && MFI.estimateStackSize(MF) > 0xffff) {
    int FI = MFI.CreateSpillStackObject(4, Align(4));
    RS->addScavengingFrameIndex(FI);
  }
}

void RXFrameLowering::determineCalleeSaves(MachineFunction &MF,
                                           BitVector &SavedRegs,
                                           RegScavenger *RS) const {
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);
  if (hasFP(MF))
    SavedRegs.set(RX::R6);
  if (hasBP(MF))
    SavedRegs.set(RX::R7);

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
  // The outgoing argument area is reserved in the prologue, except in a
  // function with dynamic allocas: there SP moves, so each call allocates
  // its area (add #-N, r0 before it, add #N, r0 after it). N is rounded up
  // to the stack alignment: a byval argument takes its own size, so the
  // arguments may end at an odd offset.
  if (!hasReservedCallFrame(MF)) {
    int64_t Amount = alignTo(I->getOperand(0).getImm(), getStackAlign());
    if (Amount) {
      if (I->getOpcode() == RX::ADJCALLSTACKDOWN)
        Amount = -Amount;
      adjustSP(MBB, I, I->getDebugLoc(), *STI.getInstrInfo(), Amount,
               MachineInstr::NoFlags);
    }
  }
  return MBB.erase(I);
}
