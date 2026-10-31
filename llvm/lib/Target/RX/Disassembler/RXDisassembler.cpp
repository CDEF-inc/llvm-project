//===- RXDisassembler.cpp - Disassembler for RX -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the RXDisassembler class.
//
// RX instructions are 1 to 8 bytes long, and no instruction is a prefix of a
// longer one, so the decoder tables are tried from the shortest length up.
// The first byte in memory is the most significant byte of `Inst` (see
// RXInstrFormats.td).
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/RXMCTargetDesc.h"
#include "TargetInfo/RXTargetInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDecoder.h"
#include "llvm/MC/MCDecoderOps.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;
using namespace llvm::MCD;

#define DEBUG_TYPE "rx-disassembler"

typedef MCDisassembler::DecodeStatus DecodeStatus;

namespace {
class RXDisassembler : public MCDisassembler {
public:
  RXDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx)
      : MCDisassembler(STI, Ctx) {}

  DecodeStatus getInstruction(MCInst &MI, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &CStream) const override;
};
} // end anonymous namespace

static MCDisassembler *createRXDisassembler(const Target &T,
                                            const MCSubtargetInfo &STI,
                                            MCContext &Ctx) {
  return new RXDisassembler(STI, Ctx);
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeRXDisassembler() {
  TargetRegistry::RegisterMCDisassembler(getTheRXTarget(),
                                         createRXDisassembler);
}

// Registers by encoding. The register classes list them in allocation order,
// so they cannot be indexed by the field.
static const MCPhysReg GPRDecoderTable[] = {
    RX::R0, RX::R1, RX::R2,  RX::R3,  RX::R4,  RX::R5,  RX::R6,  RX::R7,
    RX::R8, RX::R9, RX::R10, RX::R11, RX::R12, RX::R13, RX::R14, RX::R15};

static const MCPhysReg DPRDecoderTable[] = {
    RX::DR0, RX::DR1, RX::DR2,  RX::DR3,  RX::DR4,  RX::DR5,
    RX::DR6, RX::DR7, RX::DR8,  RX::DR9,  RX::DR10, RX::DR11,
    RX::DR12, RX::DR13, RX::DR14, RX::DR15};

static const MCPhysReg DRHDecoderTable[] = {
    RX::DRH0, RX::DRH1, RX::DRH2,  RX::DRH3,  RX::DRH4,  RX::DRH5,
    RX::DRH6, RX::DRH7, RX::DRH8,  RX::DRH9,  RX::DRH10, RX::DRH11,
    RX::DRH12, RX::DRH13, RX::DRH14, RX::DRH15};

static const MCPhysReg DRLDecoderTable[] = {
    RX::DRL0, RX::DRL1, RX::DRL2,  RX::DRL3,  RX::DRL4,  RX::DRL5,
    RX::DRL6, RX::DRL7, RX::DRL8,  RX::DRL9,  RX::DRL10, RX::DRL11,
    RX::DRL12, RX::DRL13, RX::DRL14, RX::DRL15};

static const MCPhysReg DCRDecoderTable[] = {RX::DPSW, RX::DCMR, RX::DECNT,
                                           RX::DEPC};

static const MCPhysReg ACCDecoderTable[] = {RX::ACC0, RX::ACC1};

// Control register numbers (cr) of MVTC / MVFC / PUSHC / POPC; 0 marks a
// reserved number.
static const MCPhysReg CTRLDecoderTable[] = {
    RX::PSW,  RX::PC,  RX::USP,   RX::FPSW, 0,       RX::IVC,
    0,        0,       RX::BPSW,  RX::BPC,  RX::ISP, RX::FINTV,
    RX::INTB, RX::EXTB, RX::WPRI, RX::WID};

// The root-world control registers of TRMVTC / TRMVFC (RXv3e).
static const MCPhysReg TRCTRLDecoderTable[] = {
    0,        0,        0,        0,      0,       RX::IVC,
    0,        0,        RX::BPSW, RX::BPC, RX::ISP, 0,
    RX::INTB, RX::EXTB, RX::TRCTB, RX::WINTB};

template <size_t N>
static DecodeStatus decodeReg(MCInst &MI, uint64_t RegNo,
                              const MCPhysReg (&Table)[N]) {
  if (RegNo >= N || Table[RegNo] == 0)
    return MCDisassembler::Fail;
  MI.addOperand(MCOperand::createReg(Table[RegNo]));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeGPRRegisterClass(MCInst &MI, uint64_t RegNo,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder) {
  return decodeReg(MI, RegNo, GPRDecoderTable);
}

static DecodeStatus DecodeGPRLowRegisterClass(MCInst &MI, uint64_t RegNo,
                                              uint64_t Address,
                                              const MCDisassembler *Decoder) {
  if (RegNo > 7)
    return MCDisassembler::Fail;
  return decodeReg(MI, RegNo, GPRDecoderTable);
}

static DecodeStatus DecodeDPRRegisterClass(MCInst &MI, uint64_t RegNo,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder) {
  return decodeReg(MI, RegNo, DPRDecoderTable);
}

static DecodeStatus DecodeDRHRegisterClass(MCInst &MI, uint64_t RegNo,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder) {
  return decodeReg(MI, RegNo, DRHDecoderTable);
}

static DecodeStatus DecodeDRLRegisterClass(MCInst &MI, uint64_t RegNo,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder) {
  return decodeReg(MI, RegNo, DRLDecoderTable);
}

static DecodeStatus DecodeDCRRegisterClass(MCInst &MI, uint64_t RegNo,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder) {
  return decodeReg(MI, RegNo, DCRDecoderTable);
}

static DecodeStatus DecodeACCRegisterClass(MCInst &MI, uint64_t RegNo,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder) {
  return decodeReg(MI, RegNo, ACCDecoderTable);
}

static DecodeStatus DecodeCTRLRegisterClass(MCInst &MI, uint64_t RegNo,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder) {
  // IVC, WPRI and WID exist only on RXv3e.
  if ((RegNo == 5 || RegNo >= 14) &&
      !Decoder->getSubtargetInfo().hasFeature(RX::FeatureRXv3e))
    return MCDisassembler::Fail;
  return decodeReg(MI, RegNo, CTRLDecoderTable);
}

static DecodeStatus DecodeTRCTRLRegisterClass(MCInst &MI, uint64_t RegNo,
                                              uint64_t Address,
                                              const MCDisassembler *Decoder) {
  return decodeReg(MI, RegNo, TRCTRLDecoderTable);
}

template <unsigned Bits>
static DecodeStatus decodeSImm(MCInst &MI, uint64_t Imm, uint64_t Address,
                               const MCDisassembler *Decoder) {
  MI.addOperand(MCOperand::createImm(SignExtend64<Bits>(Imm)));
  return MCDisassembler::Success;
}

// Displacements and the RTSD adjustment, scaled back to bytes.
template <unsigned Shift>
static DecodeStatus decodeScaled(MCInst &MI, uint64_t Imm, uint64_t Address,
                                 const MCDisassembler *Decoder) {
  MI.addOperand(MCOperand::createImm(Imm << Shift));
  return MCDisassembler::Success;
}

// bra.s / beq.s / bne.s: displacements 3..10, with 8, 9, 10 encoded as 0, 1,
// 2.
static DecodeStatus decodeBrTarget3(MCInst &MI, uint64_t Imm, uint64_t Address,
                                    const MCDisassembler *Decoder) {
  MI.addOperand(MCOperand::createImm(Imm < 3 ? Imm + 8 : Imm));
  return MCDisassembler::Success;
}

// MVFAC* shift count from the (i, m) bit pair: 10, 11, 00 are #0, #1, #2.
static DecodeStatus decodeMvfacImm(MCInst &MI, uint64_t Imm, uint64_t Address,
                                   const MCDisassembler *Decoder) {
  static const int Shift[] = {2, -1, 0, 1};
  if (Shift[Imm] < 0)
    return MCDisassembler::Fail;
  MI.addOperand(MCOperand::createImm(Shift[Imm]));
  return MCDisassembler::Success;
}

// RACW / RACL / RDACW / RDACL #1 or #2.
static DecodeStatus decodeRacImm(MCInst &MI, uint64_t Imm, uint64_t Address,
                                 const MCDisassembler *Decoder) {
  MI.addOperand(MCOperand::createImm(Imm + 1));
  return MCDisassembler::Success;
}

// The last register of a DPUSHM / DPOPM range, encoded as the distance from
// the first one (the operand just decoded).
template <size_t N>
static DecodeStatus decodeRange(MCInst &MI, uint64_t Dist,
                                const MCDisassembler *Decoder,
                                const MCPhysReg (&Table)[N]) {
  const MCRegisterInfo *MRI = Decoder->getContext().getRegisterInfo();
  unsigned Lo = MRI->getEncodingValue(
      MI.getOperand(MI.getNumOperands() - 1).getReg());
  return decodeReg(MI, Lo + Dist, Table);
}

static DecodeStatus decodeDPRRange(MCInst &MI, uint64_t Dist, uint64_t Address,
                                   const MCDisassembler *Decoder) {
  return decodeRange(MI, Dist, Decoder, DPRDecoderTable);
}

static DecodeStatus decodeDCRRange(MCInst &MI, uint64_t Dist, uint64_t Address,
                                   const MCDisassembler *Decoder) {
  return decodeRange(MI, Dist, Decoder, DCRDecoderTable);
}

// bfmov / bfmovz #slsb, #dlsb, #width, Rs, Rd: the 16-bit field (bytes 3
// and 4, little-endian) is ((dlsb + width) & 31) << 10 | dlsb << 5 |
// ((dlsb - slsb) & 31).
template <typename InsnType>
static DecodeStatus decodeBFMov(MCInst &MI, InsnType Insn, uint64_t Address,
                                const MCDisassembler *Decoder) {
  unsigned Rs = fieldFromInstruction(Insn, 20, 4);
  unsigned Rd = fieldFromInstruction(Insn, 16, 4);
  unsigned Field = fieldFromInstruction(Insn, 8, 8) |
                   fieldFromInstruction(Insn, 0, 8) << 8;
  unsigned DLsb = (Field >> 5) & 31;
  unsigned SLsb = (DLsb - Field) & 31;
  unsigned Width = ((Field >> 10) - DLsb) & 31;
  if (Field >> 15 || Width == 0)
    return MCDisassembler::Fail;
  // Rd; for bfmov also the tied source (the same register).
  MI.addOperand(MCOperand::createReg(GPRDecoderTable[Rd]));
  if (MI.getOpcode() == RX::BFMOV)
    MI.addOperand(MCOperand::createReg(GPRDecoderTable[Rd]));
  MI.addOperand(MCOperand::createImm(SLsb));
  MI.addOperand(MCOperand::createImm(DLsb));
  MI.addOperand(MCOperand::createImm(Width));
  MI.addOperand(MCOperand::createReg(GPRDecoderTable[Rs]));
  return MCDisassembler::Success;
}

#include "RXGenDisassemblerTables.inc"

static const uint8_t *const DecoderTables[] = {
    DecoderTableRX8,  DecoderTableRX16, DecoderTableRX24, DecoderTableRX32,
    DecoderTableRX40, DecoderTableRX48, DecoderTableRX56, DecoderTableRX64};

DecodeStatus RXDisassembler::getInstruction(MCInst &MI, uint64_t &Size,
                                            ArrayRef<uint8_t> Bytes,
                                            uint64_t Address,
                                            raw_ostream &CStream) const {
  uint64_t Insn = 0;
  for (unsigned Len = 1; Len <= 8 && Len <= Bytes.size(); ++Len) {
    Insn = Insn << 8 | Bytes[Len - 1];
    // The RXv1 DSP forms (A0, no shift) are the RXv2 ones with fields fixed.
    if (Len == 3 && !STI.hasFeature(RX::FeatureRXv2)) {
      DecodeStatus Result =
          decodeInstruction(DecoderTableRXv124, MI, Insn, Address, this, STI);
      if (Result != MCDisassembler::Fail) {
        Size = Len;
        return Result;
      }
    }
    DecodeStatus Result =
        decodeInstruction(DecoderTables[Len - 1], MI, Insn, Address, this, STI);
    if (Result != MCDisassembler::Fail) {
      Size = Len;
      return Result;
    }
  }
  // Skip one byte and try again from the next.
  Size = 1;
  return MCDisassembler::Fail;
}
