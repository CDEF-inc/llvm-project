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
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
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

void RXMCCodeEmitter::encodeInstruction(const MCInst &MI,
                                        SmallVectorImpl<char> &CB,
                                        SmallVectorImpl<MCFixup> &Fixups,
                                        const MCSubtargetInfo &STI) const {
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
