//===- RXInstrInfo.cpp - RX Instruction Information -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the RX implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "RXInstrInfo.h"
#include "RX.h"
#include "RXSubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

#define GET_INSTRINFO_CTOR_DTOR
#include "RXGenInstrInfo.inc"

void RXInstrInfo::anchor() {}

RXInstrInfo::RXInstrInfo(const RXSubtarget &STI)
    : RXGenInstrInfo(STI, RI, RX::ADJCALLSTACKDOWN, RX::ADJCALLSTACKUP), RI() {}

void RXInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator I,
                              const DebugLoc &DL, Register DestReg,
                              Register SrcReg, bool KillSrc,
                              bool RenamableDest, bool RenamableSrc) const {
  if (!RX::GPRRegClass.contains(DestReg, SrcReg))
    llvm_unreachable("RX: unsupported register copy");
  BuildMI(MBB, I, DL, get(RX::MOV_L_rr), DestReg)
      .addReg(SrcReg, getKillRegState(KillSrc));
}

void RXInstrInfo::storeRegToStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register SrcReg,
    bool isKill, int FrameIdx, const TargetRegisterClass *RC, Register VReg,
    MachineInstr::MIFlag Flags) const {
  DebugLoc DL;
  if (MI != MBB.end())
    DL = MI->getDebugLoc();
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FrameIdx),
      MachineMemOperand::MOStore, MFI.getObjectSize(FrameIdx),
      MFI.getObjectAlign(FrameIdx));

  assert(RC == &RX::GPRRegClass && "RX: cannot spill this register class");
  BuildMI(MBB, MI, DL, get(RX::ST_L))
      .addReg(SrcReg, getKillRegState(isKill))
      .addFrameIndex(FrameIdx)
      .addImm(0)
      .addMemOperand(MMO)
      .setMIFlag(Flags);
}

void RXInstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator MI,
                                       Register DestReg, int FrameIdx,
                                       const TargetRegisterClass *RC,
                                       Register VReg, unsigned SubReg,
                                       MachineInstr::MIFlag Flags) const {
  DebugLoc DL;
  if (MI != MBB.end())
    DL = MI->getDebugLoc();
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FrameIdx),
      MachineMemOperand::MOLoad, MFI.getObjectSize(FrameIdx),
      MFI.getObjectAlign(FrameIdx));

  assert(RC == &RX::GPRRegClass && "RX: cannot reload this register class");
  BuildMI(MBB, MI, DL, get(RX::LD_L), DestReg)
      .addFrameIndex(FrameIdx)
      .addImm(0)
      .addMemOperand(MMO)
      .setMIFlag(Flags);
}

unsigned RXInstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  case TargetOpcode::INLINEASM:
  case TargetOpcode::INLINEASM_BR: {
    const MachineFunction *MF = MI.getParent()->getParent();
    return getInlineAsmLength(MI.getOperand(0).getSymbolName(),
                              MF->getTarget().getMCAsmInfo());
  }
  default:
    // The longest form; RXMCInstLower may emit a shorter one.
    return MI.getDesc().getSize();
  }
}

static bool isUncondBranch(unsigned Opc) { return Opc == RX::BRA_A; }

bool RXInstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                MachineBasicBlock *&TBB,
                                MachineBasicBlock *&FBB,
                                SmallVectorImpl<MachineOperand> &Cond,
                                bool AllowModify) const {
  MachineBasicBlock::iterator I = MBB.end();
  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr())
      continue;
    if (!isUnpredicatedTerminator(*I))
      break;
    if (!I->isBranch() || I->isIndirectBranch())
      return true;

    if (isUncondBranch(I->getOpcode())) {
      if (!AllowModify) {
        TBB = I->getOperand(0).getMBB();
        continue;
      }
      // Delete anything after the unconditional branch.
      MBB.erase(std::next(I), MBB.end());
      Cond.clear();
      FBB = nullptr;
      if (MBB.isLayoutSuccessor(I->getOperand(0).getMBB())) {
        TBB = nullptr;
        I->eraseFromParent();
        I = MBB.end();
        continue;
      }
      TBB = I->getOperand(0).getMBB();
      continue;
    }

    assert(I->getOpcode() == RX::BCC && "unexpected conditional branch");
    if (Cond.empty()) {
      FBB = TBB;
      TBB = I->getOperand(0).getMBB();
      Cond.push_back(I->getOperand(1));
      continue;
    }
    // Two conditional branches: only handle identical ones.
    if (TBB != I->getOperand(0).getMBB() ||
        Cond[0].getImm() != I->getOperand(1).getImm())
      return true;
  }
  return false;
}

unsigned RXInstrInfo::removeBranch(MachineBasicBlock &MBB,
                                   int *BytesRemoved) const {
  MachineBasicBlock::iterator I = MBB.end();
  unsigned Count = 0;
  int Bytes = 0;
  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr())
      continue;
    if (I->getOpcode() != RX::BRA_A && I->getOpcode() != RX::BCC)
      break;
    Bytes += getInstSizeInBytes(*I);
    I->eraseFromParent();
    I = MBB.end();
    ++Count;
  }
  if (BytesRemoved)
    *BytesRemoved = Bytes;
  return Count;
}

unsigned RXInstrInfo::insertBranch(MachineBasicBlock &MBB,
                                   MachineBasicBlock *TBB,
                                   MachineBasicBlock *FBB,
                                   ArrayRef<MachineOperand> Cond,
                                   const DebugLoc &DL, int *BytesAdded) const {
  assert(TBB && "insertBranch must not be told to insert a fallthrough");
  assert((Cond.size() == 1 || Cond.empty()) &&
         "RX branch conditions have one component");

  int Bytes = 0;
  unsigned Count = 0;
  if (Cond.empty()) {
    assert(!FBB && "unconditional branch with multiple successors");
    MachineInstr &MI = *BuildMI(&MBB, DL, get(RX::BRA_A)).addMBB(TBB);
    Bytes += getInstSizeInBytes(MI);
    Count = 1;
  } else {
    MachineInstr &MI =
        *BuildMI(&MBB, DL, get(RX::BCC)).addMBB(TBB).add(Cond[0]);
    Bytes += getInstSizeInBytes(MI);
    Count = 1;
    if (FBB) {
      MachineInstr &MI2 = *BuildMI(&MBB, DL, get(RX::BRA_A)).addMBB(FBB);
      Bytes += getInstSizeInBytes(MI2);
      ++Count;
    }
  }
  if (BytesAdded)
    *BytesAdded = Bytes;
  return Count;
}

bool RXInstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  assert(Cond.size() == 1 && "invalid RX branch condition");
  auto CC = static_cast<RXCC::CondCode>(Cond[0].getImm());
  Cond[0].setImm(RXCC::getInverse(CC));
  return false;
}

MachineBasicBlock *
RXInstrInfo::getBranchDestBlock(const MachineInstr &MI) const {
  return MI.getOperand(0).getMBB();
}

bool RXInstrInfo::isBranchOffsetInRange(unsigned BranchOpc,
                                        int64_t BrOffset) const {
  // Displacements are relative to the start of the branch instruction.
  switch (BranchOpc) {
  case RX::BCC:
    return isInt<8>(BrOffset);
  case RX::BRA_A:
    return isInt<24>(BrOffset);
  default:
    llvm_unreachable("unexpected branch opcode");
  }
}
