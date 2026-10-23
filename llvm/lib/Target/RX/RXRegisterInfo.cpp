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

// The return address lives on the stack; there is no link register.
RXRegisterInfo::RXRegisterInfo() : RXGenRegisterInfo(RX::R0) {}

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
  for (MCPhysReg Reg : RX::CTRLRegClass)
    Reserved.set(Reg);
  Reserved.set(RX::ACC0);
  Reserved.set(RX::ACC1);
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
    return 2;
  case RX::LD_L:
  case RX::ST_L:
    return 4;
  default:
    return 1;
  }
}

bool RXRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                         int SPAdj, unsigned FIOperandNum,
                                         RegScavenger *RS) const {
  assert(SPAdj == 0 && "unexpected SP adjustment");

  MachineInstr &MI = *II;
  MachineFunction &MF = *MI.getParent()->getParent();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();

  // Offsets are relative to the CFA (SP before the call pushed the return
  // address); SP now sits StackSize bytes below the return address.
  int64_t Offset = MFI.getObjectOffset(FrameIndex) + 4 + MFI.getStackSize() +
                   MI.getOperand(FIOperandNum + 1).getImm();

  if (MI.getOpcode() == RX::FRMIDX) {
    // add #Offset, r0, rd
    const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
    MI.setDesc(TII.get(RX::ADD_rri32));
    MI.getOperand(FIOperandNum).ChangeToRegister(RX::R0, false);
    MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Offset);
    return false;
  }

  if (!isEncodableDisp(Offset, getAccessScale(MI.getOpcode())))
    report_fatal_error("RX: stack offset not encodable in a dsp:16 access");

  MI.getOperand(FIOperandNum).ChangeToRegister(RX::R0, false);
  MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Offset);
  return false;
}

Register RXRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  return RX::R0;
}
