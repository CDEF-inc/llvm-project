//===- RXRegisterInfo.cpp - RX Register Information -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the RX implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#include "RXRegisterInfo.h"
#include "RX.h"
#include "RXSubtarget.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define DEBUG_TYPE "rx-reg-info"

#define GET_REGINFO_TARGET_DESC
#include "RXGenRegisterInfo.inc"

// The return address lives on the stack; there is no link register. PC
// names the return address column of the CFI, as in GCC.
RXRegisterInfo::RXRegisterInfo() : RXGenRegisterInfo(RX::PC) {}

const MCPhysReg *
RXRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  return CSR_RX_SaveList;
}

const uint32_t *RXRegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                                     CallingConv::ID) const {
  return CSR_RX_RegMask;
}

BitVector RXRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  Reserved.set(RX::R0); // SP
  const RXFrameLowering *TFI =
      MF.getSubtarget<RXSubtarget>().getFrameLowering();
  if (TFI->hasFP(MF))
    Reserved.set(RX::R6);
  if (TFI->hasBP(MF)) // the base pointer
    Reserved.set(RX::R7);
  for (MCPhysReg Reg : RX::CTRLRegClass)
    Reserved.set(Reg);
  for (MCPhysReg Reg : RX::DCRRegClass) // DPSW, ...
    Reserved.set(Reg);
  Reserved.set(RX::ACC0);
  Reserved.set(RX::ACC1);
  // Code generation uses only DR0-DR3, the DR registers that rx-rt's
  // interrupt entry saves (with the dfpu feature); all of them are
  // caller-saved.
  for (MCPhysReg Reg : RX::DPRRegClass)
    if (Reg != RX::DR0 && Reg != RX::DR1 && Reg != RX::DR2 && Reg != RX::DR3)
      Reserved.set(Reg);
  return Reserved;
}

const TargetRegisterClass *
RXRegisterInfo::getPointerRegClass(unsigned Kind) const {
  return &RX::GPRRegClass;
}

// Whether a byte offset fits the dsp:16 field of an access of the given size.
static bool isEncodableDisp(int64_t Offset, unsigned Scale) {
  return Offset >= 0 && Offset % Scale == 0 && Offset / Scale <= 0xffff;
}

static unsigned getAccessScale(unsigned Opc) {
  switch (Opc) {
  case RX::LD_W:
  case RX::LD_UW:
  case RX::ST_W:
  case RX::STI_W:
    return 2;
  case RX::LD_L:
  case RX::ST_L:
  case RX::STI_L:
  case RX::FADD_mP:
  case RX::FSUB_mP:
  case RX::FMUL_mP:
  case RX::FDIV_mP:
  case RX::FCMP_mP:
  case RX::FSQRT_mP:
  case RX::DST_D: // dmov.d: the displacement is scaled by 4
  case RX::DLD_D:
    return 4;
  default:
    return 1;
  }
}

// Whether an instruction at MI's place may read the flags (PSW) that an
// earlier one set: a later instruction reads them before setting them, or
// they are live into a successor.
static bool isPSWLiveAt(const MachineInstr &MI) {
  const MachineBasicBlock &MBB = *MI.getParent();
  for (MachineBasicBlock::const_iterator I = std::next(MI.getIterator()),
                                         E = MBB.end();
       I != E; ++I) {
    if (I->readsRegister(RX::PSW, /*TRI=*/nullptr))
      return true;
    if (I->definesRegister(RX::PSW, /*TRI=*/nullptr))
      return false;
  }
  for (const MachineBasicBlock *Succ : MBB.successors())
    if (Succ->isLiveIn(RX::PSW))
      return true;
  return false;
}

bool RXRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                         int SPAdj, unsigned FIOperandNum,
                                         RegScavenger *RS) const {
  assert(SPAdj == 0 && "unexpected SP adjustment");

  MachineInstr &MI = *II;
  MachineFunction &MF = *MI.getParent()->getParent();
  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();

  // RXFrameLowering::getFrameIndexReference picks SP, R6 or R7.
  Register Base;
  int64_t Offset = MF.getSubtarget<RXSubtarget>()
                       .getFrameLowering()
                       ->getFrameIndexReference(MF, FrameIndex, Base)
                       .getFixed() +
                   MI.getOperand(FIOperandNum + 1).getImm();

  if (MI.getOpcode() == RX::FRMIDX) {
    // add #Offset, r0 (or r6 / r7), rd
    const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
    MI.setDesc(TII.get(RX::ADD_rri32));
    MI.getOperand(FIOperandNum).ChangeToRegister(Base, false);
    MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Offset);
    return false;
  }

  if (!isEncodableDisp(Offset, getAccessScale(MI.getOpcode()))) {
    // Beyond dsp:16 (64 K accesses up from the base), or not a multiple of
    // the access size: add #Offset, base, tmp, and the access at [tmp]
    // (PEI's scavenger picks tmp; RXFrameLowering keeps it a slot to
    // spill to in a large frame). add sets the flags, which must not be
    // live here.
    if (isPSWLiveAt(MI))
      report_fatal_error("RX: stack offset not encodable in a dsp:16 access "
                         "while the flags are live");
    MachineBasicBlock &MBB = *MI.getParent();
    const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
    Register Tmp = MF.getRegInfo().createVirtualRegister(&RX::GPRRegClass);
    BuildMI(MBB, II, MI.getDebugLoc(), TII.get(RX::ADD_rri32), Tmp)
        .addReg(Base)
        .addImm(Offset);
    MI.getOperand(FIOperandNum).ChangeToRegister(Tmp, false, false, true);
    MI.getOperand(FIOperandNum + 1).ChangeToImmediate(0);
    return false;
  }

  MI.getOperand(FIOperandNum).ChangeToRegister(Base, false);
  MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Offset);
  return false;
}

// The prologue realigns SP for a fixed-size stack object aligned beyond the
// stack alignment (see RXFrameLowering::hasFPImpl), or when the function
// asks for it. A dynamic alloca aligns its own address (and #-align after
// moving SP), so it needs no realignment, unlike in the generic check of
// MaxAlign.
bool RXRegisterInfo::shouldRealignStack(const MachineFunction &MF) const {
  const Function &F = MF.getFunction();
  if (F.hasFnAttribute(Attribute::StackAlignment) ||
      F.hasFnAttribute("stackrealign"))
    return TargetRegisterInfo::shouldRealignStack(MF);
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  Align StackAlign = MF.getSubtarget().getFrameLowering()->getStackAlign();
  if (MFI.getMaxAlign() <= StackAlign)
    return false;
  for (int I = MFI.getObjectIndexBegin(), E = MFI.getObjectIndexEnd(); I != E;
       ++I)
    if (!MFI.isDeadObjectIndex(I) && !MFI.isVariableSizedObjectIndex(I) &&
        MFI.getObjectAlign(I) > StackAlign)
      return true;
  return false;
}

Register RXRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  return getFrameLowering(MF)->hasFP(MF) ? RX::R6 : RX::R0;
}

// Inline assembly that clobbers SP, R6 in a function with a frame pointer,
// or R7 in one with a base pointer, gets the "clobber list contains reserved
// registers" warning.
bool RXRegisterInfo::isAsmClobberable(const MachineFunction &MF,
                                      MCRegister PhysReg) const {
  return PhysReg != RX::R0 && PhysReg != getFrameRegister(MF).asMCReg() &&
         !(PhysReg == RX::R7 &&
           MF.getSubtarget<RXSubtarget>().getFrameLowering()->hasBP(MF));
}
