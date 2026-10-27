//===- RXMCCodeEmitter.cpp - Convert RX code to machine code ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the RXMCCodeEmitter class.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/RXFixupKinds.h"
#include "MCTargetDesc/RXMCTargetDesc.h"
#include "MCTargetDesc/RXSizelessBranch.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"

#define DEBUG_TYPE "mccodeemitter"

namespace llvm {

class RXMCCodeEmitter : public MCCodeEmitter {
  MCContext &Ctx;
  const MCInstrInfo &MCII;

  /// TableGen'erated function for getting the binary encoding for an
  /// instruction. The first byte in memory is the most significant byte of
  /// the low Size bytes.
  uint64_t getBinaryCodeForInstr(const MCInst &MI,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const;

  unsigned getMachineOpValue(const MCInst &MI, const MCOperand &MO,
                             SmallVectorImpl<MCFixup> &Fixups,
                             const MCSubtargetInfo &STI) const;

  // Immediate or displacement divided by 2^Shift (displacements are scaled by
  // the access size, the RTSD adjustment by 4).
  template <unsigned Shift>
  unsigned getScaledOpValue(const MCInst &MI, unsigned OpNo,
                            SmallVectorImpl<MCFixup> &Fixups,
                            const MCSubtargetInfo &STI) const;

  // 32-bit immediate; a symbolic value becomes an R_RX_DIR32 in the last four
  // bytes of the instruction.
  unsigned getImm32OpValue(const MCInst &MI, unsigned OpNo,
                           SmallVectorImpl<MCFixup> &Fixups,
                           const MCSubtargetInfo &STI) const;

  // MVFAC* shift count #0, #1, #2 as the (i, m) bit pair 10, 11, 00.
  unsigned getMvfacImmOpValue(const MCInst &MI, unsigned OpNo,
                              SmallVectorImpl<MCFixup> &Fixups,
                              const MCSubtargetInfo &STI) const;

  // RACW / RACL / RDACW / RDACL #1 or #2, encoded as 0 or 1.
  unsigned getRacImmOpValue(const MCInst &MI, unsigned OpNo,
                            SmallVectorImpl<MCFixup> &Fixups,
                            const MCSubtargetInfo &STI) const;

  // The last register of a DPUSHM/DPOPM range, as the distance from the
  // first one (the previous operand).
  unsigned getRegRangeOpValue(const MCInst &MI, unsigned OpNo,
                              SmallVectorImpl<MCFixup> &Fixups,
                              const MCSubtargetInfo &STI) const;

  // BFMOV/BFMOVZ: the 16-bit field built from #slsb, #dlsb (the two previous
  // operands) and #width.
  unsigned getBfieldOpValue(const MCInst &MI, unsigned OpNo,
                            SmallVectorImpl<MCFixup> &Fixups,
                            const MCSubtargetInfo &STI) const;

  unsigned getBrTargetOpValue(const MCInst &MI, unsigned OpNo,
                              SmallVectorImpl<MCFixup> &Fixups,
                              MCFixupKind Kind, unsigned Offset) const;
  unsigned getBrTarget3OpValue(const MCInst &MI, unsigned OpNo,
                               SmallVectorImpl<MCFixup> &Fixups,
                               const MCSubtargetInfo &STI) const;
  unsigned getBrTarget8OpValue(const MCInst &MI, unsigned OpNo,
                               SmallVectorImpl<MCFixup> &Fixups,
                               const MCSubtargetInfo &STI) const;
  unsigned getBrTarget16OpValue(const MCInst &MI, unsigned OpNo,
                                SmallVectorImpl<MCFixup> &Fixups,
                                const MCSubtargetInfo &STI) const;
  unsigned getBrTarget24OpValue(const MCInst &MI, unsigned OpNo,
                                SmallVectorImpl<MCFixup> &Fixups,
                                const MCSubtargetInfo &STI) const;

public:
  RXMCCodeEmitter(const MCInstrInfo &MCII, MCContext &Ctx)
      : Ctx(Ctx), MCII(MCII) {}

  void encodeInstruction(const MCInst &MI, SmallVectorImpl<char> &CB,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const override;
};

// The instructions a BR_SIZELESS (target, kind, step) stands for.
static void expandSizelessBranch(const MCInst &MI,
                                 SmallVectorImpl<MCInst> &Out) {
  static const unsigned CondB[] = {
      RX::BEQ_B, RX::BNE_B, RX::BGEU_B, RX::BLTU_B, RX::BGTU_B,
      RX::BLEU_B, RX::BPZ_B, RX::BN_B, RX::BGE_B, RX::BLT_B,
      RX::BGT_B, RX::BLE_B, RX::BO_B, RX::BNO_B};
  const MCOperand &Dst = MI.getOperand(0);
  unsigned Kind = MI.getOperand(1).getImm();
  RX::SizelessBranchForm Form =
      RX::getSizelessBranchForms(Kind)[MI.getOperand(2).getImm()];
  auto Add = [&](unsigned Opc, const MCOperand &Op) {
    Out.push_back(MCInstBuilder(Opc).addOperand(Op));
  };
  if (Kind == RX::BrKindBra) {
    static const unsigned Bra[] = {RX::BRA_S, RX::BRA_B, RX::BRA_W, RX::BRA_A};
    Add(Bra[Form], Dst);
    return;
  }
  if (Kind == RX::BrKindBsr) {
    Add(Form == RX::BrFormW ? RX::BSR_W : RX::BSR_A, Dst);
    return;
  }
  // The inverted condition is the other one of its pair (eq / ne, ...).
  unsigned Cond = Kind - RX::BrKindCond, Inv = Cond ^ 1;
  switch (Form) {
  case RX::BrFormS:
    Add(Cond == 0 ? RX::BEQ_S : RX::BNE_S, Dst);
    return;
  case RX::BrFormB:
    Add(CondB[Cond], Dst);
    return;
  case RX::BrFormW:
    Add(Cond == 0 ? RX::BEQ_W : RX::BNE_W, Dst);
    return;
  case RX::BrFormInvSA:
    Add(Inv == 0 ? RX::BEQ_S : RX::BNE_S, MCOperand::createImm(5));
    Add(RX::BRA_A, Dst);
    return;
  case RX::BrFormInvBW:
    Add(CondB[Inv], MCOperand::createImm(5));
    Add(RX::BRA_W, Dst);
    return;
  case RX::BrFormInvBA:
    Add(CondB[Inv], MCOperand::createImm(6));
    Add(RX::BRA_A, Dst);
    return;
  default:
    llvm_unreachable("no such form for a conditional branch");
  }
}

void RXMCCodeEmitter::encodeInstruction(const MCInst &MI,
                                        SmallVectorImpl<char> &CB,
                                        SmallVectorImpl<MCFixup> &Fixups,
                                        const MCSubtargetInfo &STI) const {
  if (MI.getOpcode() == RX::BR_SIZELESS) {
    SmallVector<MCInst, 2> Insts;
    expandSizelessBranch(MI, Insts);
    size_t Start = CB.size();
    for (const MCInst &I : Insts) {
      uint32_t Offset = CB.size() - Start;
      SmallVector<MCFixup, 1> InstFixups;
      encodeInstruction(I, CB, InstFixups, STI);
      for (MCFixup F : InstFixups) {
        F.setOffset(F.getOffset() + Offset);
        Fixups.push_back(F);
      }
    }
    return;
  }
  unsigned Size = MCII.get(MI.getOpcode()).getSize();
  uint64_t Bits = getBinaryCodeForInstr(MI, Fixups, STI);
  for (unsigned I = Size; I != 0; --I)
    CB.push_back(char((Bits >> ((I - 1) * 8)) & 0xff));
}

unsigned RXMCCodeEmitter::getMachineOpValue(const MCInst &MI,
                                            const MCOperand &MO,
                                            SmallVectorImpl<MCFixup> &Fixups,
                                            const MCSubtargetInfo &STI) const {
  if (MO.isReg())
    return Ctx.getRegisterInfo()->getEncodingValue(MO.getReg());
  if (MO.isImm())
    return static_cast<unsigned>(MO.getImm());
  llvm_unreachable("symbolic operand in a field that cannot take a fixup");
}

template <unsigned Shift>
unsigned RXMCCodeEmitter::getScaledOpValue(const MCInst &MI, unsigned OpNo,
                                           SmallVectorImpl<MCFixup> &Fixups,
                                           const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  assert(MO.isImm() && "scaled operand must be a constant");
  return static_cast<unsigned>(MO.getImm()) >> Shift;
}

unsigned RXMCCodeEmitter::getImm32OpValue(const MCInst &MI, unsigned OpNo,
                                          SmallVectorImpl<MCFixup> &Fixups,
                                          const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  if (MO.isImm())
    return static_cast<unsigned>(MO.getImm());
  unsigned Size = MCII.get(MI.getOpcode()).getSize();
  Fixups.push_back(MCFixup::create(Size - 4, MO.getExpr(), FK_Data_4));
  return 0;
}

unsigned RXMCCodeEmitter::getMvfacImmOpValue(const MCInst &MI, unsigned OpNo,
                                             SmallVectorImpl<MCFixup> &Fixups,
                                             const MCSubtargetInfo &STI) const {
  static const unsigned Enc[] = {0b10, 0b11, 0b00};
  const MCOperand &MO = MI.getOperand(OpNo);
  assert(MO.isImm() && MO.getImm() >= 0 && MO.getImm() <= 2 &&
         "MVFAC shift count must be 0, 1 or 2");
  return Enc[MO.getImm()];
}

unsigned RXMCCodeEmitter::getRacImmOpValue(const MCInst &MI, unsigned OpNo,
                                           SmallVectorImpl<MCFixup> &Fixups,
                                           const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  assert(MO.isImm() && (MO.getImm() == 1 || MO.getImm() == 2) &&
         "RAC shift count must be 1 or 2");
  return static_cast<unsigned>(MO.getImm()) - 1;
}

unsigned RXMCCodeEmitter::getRegRangeOpValue(const MCInst &MI, unsigned OpNo,
                                             SmallVectorImpl<MCFixup> &Fixups,
                                             const MCSubtargetInfo &STI) const {
  const MCRegisterInfo *MRI = Ctx.getRegisterInfo();
  unsigned Lo = MRI->getEncodingValue(MI.getOperand(OpNo - 1).getReg());
  unsigned Hi = MRI->getEncodingValue(MI.getOperand(OpNo).getReg());
  assert(Hi >= Lo && "register range must be ascending");
  return Hi - Lo;
}

unsigned RXMCCodeEmitter::getBfieldOpValue(const MCInst &MI, unsigned OpNo,
                                           SmallVectorImpl<MCFixup> &Fixups,
                                           const MCSubtargetInfo &STI) const {
  unsigned SLsb = MI.getOperand(OpNo - 2).getImm();
  unsigned DLsb = MI.getOperand(OpNo - 1).getImm();
  unsigned Width = MI.getOperand(OpNo).getImm();
  return ((DLsb + Width) & 0x1f) << 10 | DLsb << 5 | ((DLsb - SLsb) & 0x1f);
}

unsigned RXMCCodeEmitter::getBrTargetOpValue(const MCInst &MI, unsigned OpNo,
                                             SmallVectorImpl<MCFixup> &Fixups,
                                             MCFixupKind Kind,
                                             unsigned Offset) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  if (MO.isImm())
    return static_cast<unsigned>(MO.getImm());
  Fixups.push_back(MCFixup::create(Offset, MO.getExpr(), Kind, /*PCRel=*/true));
  return 0;
}

unsigned RXMCCodeEmitter::getBrTarget3OpValue(const MCInst &MI, unsigned OpNo,
                                              SmallVectorImpl<MCFixup> &Fixups,
                                              const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  // A constant 3..10 is encoded modulo 8 (8, 9, 10 -> 0, 1, 2).
  if (MO.isImm())
    return static_cast<unsigned>(MO.getImm()) & 7;
  return getBrTargetOpValue(MI, OpNo, Fixups, RX::fixup_rx_pcrel3, 0);
}

unsigned RXMCCodeEmitter::getBrTarget8OpValue(const MCInst &MI, unsigned OpNo,
                                              SmallVectorImpl<MCFixup> &Fixups,
                                              const MCSubtargetInfo &STI) const {
  return getBrTargetOpValue(MI, OpNo, Fixups, RX::fixup_rx_pcrel8, 1);
}

unsigned
RXMCCodeEmitter::getBrTarget16OpValue(const MCInst &MI, unsigned OpNo,
                                      SmallVectorImpl<MCFixup> &Fixups,
                                      const MCSubtargetInfo &STI) const {
  return getBrTargetOpValue(MI, OpNo, Fixups, RX::fixup_rx_pcrel16, 1);
}

unsigned
RXMCCodeEmitter::getBrTarget24OpValue(const MCInst &MI, unsigned OpNo,
                                      SmallVectorImpl<MCFixup> &Fixups,
                                      const MCSubtargetInfo &STI) const {
  return getBrTargetOpValue(MI, OpNo, Fixups, RX::fixup_rx_pcrel24, 1);
}

MCCodeEmitter *createRXMCCodeEmitter(const MCInstrInfo &MCII, MCContext &Ctx) {
  return new RXMCCodeEmitter(MCII, Ctx);
}

#include "RXGenMCCodeEmitter.inc"

} // end namespace llvm
