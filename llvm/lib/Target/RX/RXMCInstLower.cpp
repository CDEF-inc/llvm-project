//===- RXMCInstLower.cpp - Convert RX MachineInstr to an MCInst -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains code to lower RX MachineInstrs to their corresponding
// MCInst records.
//
//===----------------------------------------------------------------------===//

#include "RXMCInstLower.h"
#include "MCTargetDesc/RXMCTargetDesc.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

MCOperand RXMCInstLower::lowerSymbolOperand(const MachineOperand &MO,
                                            MCSymbol *Sym) const {
  const MCExpr *Expr = MCSymbolRefExpr::create(Sym, Ctx);
  if (!MO.isJTI() && !MO.isMBB() && MO.getOffset())
    Expr = MCBinaryExpr::createAdd(
        Expr, MCConstantExpr::create(MO.getOffset(), Ctx), Ctx);
  return MCOperand::createExpr(Expr);
}

// Indexed by RXCC::CondCode.
static const unsigned BccOpcodes[] = {
    RX::BEQ_B, RX::BNE_B, RX::BGEU_B, RX::BLTU_B, RX::BGTU_B,
    RX::BLEU_B, RX::BPZ_B, RX::BN_B,  RX::BGE_B,  RX::BLT_B,
    RX::BGT_B, RX::BLE_B, RX::BO_B,  RX::BNO_B};
static const unsigned SccOpcodes[] = {
    RX::SCEQ_L, RX::SCNE_L, RX::SCGEU_L, RX::SCLTU_L, RX::SCGTU_L,
    RX::SCLEU_L, RX::SCPZ_L, RX::SCN_L,  RX::SCGE_L,  RX::SCLT_L,
    RX::SCGT_L, RX::SCLE_L, RX::SCO_L,  RX::SCNO_L};

// Load/store pseudos and the dsp:16 instructions they stand for (same
// operand layout: data register, base, displacement).
static unsigned getMemOpcode(unsigned Opc) {
  switch (Opc) {
  case RX::LD_L: return RX::MOV_L_ld16;
  case RX::LD_B: return RX::MOV_B_ld16;
  case RX::LD_W: return RX::MOV_W_ld16;
  case RX::LD_UB: return RX::MOVU_B_ld16;
  case RX::LD_UW: return RX::MOVU_W_ld16;
  case RX::ST_L: return RX::MOV_L_st16;
  case RX::ST_B: return RX::MOV_B_st16;
  case RX::ST_W: return RX::MOV_W_st16;
  default: return Opc;
  }
}

void RXMCInstLower::lower(const MachineInstr *MI, MCInst &OutMI) const {
  OutMI.setOpcode(getMemOpcode(MI->getOpcode()));

  for (const MachineOperand &MO : MI->operands()) {
    MCOperand MCOp;
    switch (MO.getType()) {
    case MachineOperand::MO_Register:
      if (MO.isImplicit())
        continue;
      MCOp = MCOperand::createReg(MO.getReg());
      break;
    case MachineOperand::MO_Immediate:
      MCOp = MCOperand::createImm(MO.getImm());
      break;
    case MachineOperand::MO_MachineBasicBlock:
      MCOp = MCOperand::createExpr(
          MCSymbolRefExpr::create(MO.getMBB()->getSymbol(), Ctx));
      break;
    case MachineOperand::MO_GlobalAddress:
      MCOp = lowerSymbolOperand(MO, Printer.getSymbol(MO.getGlobal()));
      break;
    case MachineOperand::MO_ExternalSymbol:
      MCOp = lowerSymbolOperand(
          MO, Printer.GetExternalSymbolSymbol(MO.getSymbolName()));
      break;
    case MachineOperand::MO_BlockAddress:
      MCOp = lowerSymbolOperand(
          MO, Printer.GetBlockAddressSymbol(MO.getBlockAddress()));
      break;
    case MachineOperand::MO_RegisterMask:
      continue;
    default:
      MI->print(errs());
      llvm_unreachable("unknown operand type");
    }
    OutMI.addOperand(MCOp);
  }

  // Condition-code pseudos (BCC dst, cc / SCC rd, cc): the condition operand
  // selects the opcode.
  if (OutMI.getOpcode() == RX::BCC || OutMI.getOpcode() == RX::SCC) {
    bool IsBranch = OutMI.getOpcode() == RX::BCC;
    unsigned CC = OutMI.getOperand(1).getImm();
    assert(CC < std::size(BccOpcodes) && "invalid condition code");
    MCInst New;
    New.setOpcode(IsBranch ? BccOpcodes[CC] : SccOpcodes[CC]);
    New.addOperand(OutMI.getOperand(0));
    OutMI = New;
  }
}

//===----------------------------------------------------------------------===//
// Encoding selection
//===----------------------------------------------------------------------===//

namespace {

// Encodings of one operation by immediate size; 0 if there is none.
struct ImmFamily {
  unsigned I32, I4, U8, S8, S16, S24;
};

// rd, src, #imm (tied) and rd, #imm forms.
const ImmFamily ImmFamilies[] = {
    {RX::MUL_ri32, RX::MUL_ri4, 0, RX::MUL_ri8, RX::MUL_ri16, RX::MUL_ri24},
    {RX::AND_ri32, RX::AND_ri4, 0, RX::AND_ri8, RX::AND_ri16, RX::AND_ri24},
    {RX::OR_ri32, RX::OR_ri4, 0, RX::OR_ri8, RX::OR_ri16, RX::OR_ri24},
    {RX::XOR_ri32, 0, 0, RX::XOR_ri8, RX::XOR_ri16, RX::XOR_ri24},
    {RX::ADC_ri32, 0, 0, RX::ADC_ri8, RX::ADC_ri16, RX::ADC_ri24},
    {RX::MAX_ri32, 0, 0, RX::MAX_ri8, RX::MAX_ri16, RX::MAX_ri24},
    {RX::MIN_ri32, 0, 0, RX::MIN_ri8, RX::MIN_ri16, RX::MIN_ri24},
    {RX::EMUL_ri32, 0, 0, RX::EMUL_ri8, RX::EMUL_ri16, RX::EMUL_ri24},
    {RX::EMULU_ri32, 0, 0, RX::EMULU_ri8, RX::EMULU_ri16, RX::EMULU_ri24},
    {RX::DIV_ri32, 0, 0, RX::DIV_ri8, RX::DIV_ri16, RX::DIV_ri24},
    {RX::DIVU_ri32, 0, 0, RX::DIVU_ri8, RX::DIVU_ri16, RX::DIVU_ri24},
    {RX::MOV_L_ri32, RX::MOV_L_ri4, RX::MOV_L_ri8, RX::MOV_L_ris8,
     RX::MOV_L_ri16, RX::MOV_L_ri24},
    {RX::CMP_ri32, RX::CMP_ri4, RX::CMP_ri8, RX::CMP_ris8, RX::CMP_ri16,
     RX::CMP_ri24},
};

// Memory accesses by displacement size: ld/st with dsp:16, [Rn], dsp:5
// (R0-R7 only), dsp:8; Scale is the access size.
struct MemFamily {
  unsigned D16, D0, D5, D8, Scale;
  bool IsStore;
};

const MemFamily MemFamilies[] = {
    {RX::MOV_B_ld16, RX::MOV_B_ld0, RX::MOV_B_ld5, RX::MOV_B_ld8, 1, false},
    {RX::MOV_W_ld16, RX::MOV_W_ld0, RX::MOV_W_ld5, RX::MOV_W_ld8, 2, false},
    {RX::MOV_L_ld16, RX::MOV_L_ld0, RX::MOV_L_ld5, RX::MOV_L_ld8, 4, false},
    {RX::MOVU_B_ld16, RX::MOVU_B_ld0, RX::MOVU_B_ld5, RX::MOVU_B_ld8, 1,
     false},
    {RX::MOVU_W_ld16, RX::MOVU_W_ld0, RX::MOVU_W_ld5, RX::MOVU_W_ld8, 2,
     false},
    {RX::MOV_B_st16, RX::MOV_B_st0, RX::MOV_B_st5, RX::MOV_B_st8, 1, true},
    {RX::MOV_W_st16, RX::MOV_W_st0, RX::MOV_W_st5, RX::MOV_W_st8, 2, true},
    {RX::MOV_L_st16, RX::MOV_L_st0, RX::MOV_L_st5, RX::MOV_L_st8, 4, true},
};

// 3-operand register forms and their 2-operand equivalents.
struct RRRFamily {
  unsigned RRR, RR;
  bool Commutative;
};

const RRRFamily RRRFamilies[] = {
    {RX::ADD_rrr, RX::ADD_rr, true},
    {RX::MUL_rrr, RX::MUL_rr, true},
    {RX::AND_rrr, RX::AND_rr, true},
    {RX::OR_rrr, RX::OR_rr, true},
    {RX::SUB_rrr, RX::SUB_rr, false},
};

bool isLowReg(MCRegister Reg) {
  switch (Reg.id()) {
  case RX::R0: case RX::R1: case RX::R2: case RX::R3:
  case RX::R4: case RX::R5: case RX::R6: case RX::R7:
    return true;
  default:
    return false;
  }
}

void setOpcodeAndOperands(MCInst &Inst, unsigned Opc,
                          ArrayRef<MCOperand> Ops) {
  MCInst New;
  New.setOpcode(Opc);
  for (const MCOperand &Op : Ops)
    New.addOperand(Op);
  Inst = New;
}

bool shrinkImm(MCInst &Inst) {
  for (const ImmFamily &F : ImmFamilies) {
    if (Inst.getOpcode() != F.I32)
      continue;
    const MCOperand &ImmOp = Inst.getOperand(Inst.getNumOperands() - 1);
    if (!ImmOp.isImm())
      return true; // symbolic: keep the 32-bit form
    int64_t V = SignExtend64<32>(ImmOp.getImm());
    unsigned Opc = F.I32;
    if (F.I4 && V >= 0 && V <= 15)
      Opc = F.I4;
    else if (F.U8 && V >= 0 && V <= 255)
      Opc = F.U8;
    else if (F.S8 && isInt<8>(V) && (!F.U8 || V < 0))
      Opc = F.S8;
    else if (isInt<16>(V))
      Opc = F.S16;
    else if (isInt<24>(V))
      Opc = F.S24;
    Inst.setOpcode(Opc);
    Inst.getOperand(Inst.getNumOperands() - 1).setImm(V);
    return true;
  }
  return false;
}

// add #imm, Rs, Rd
bool shrinkAddImm(MCInst &Inst) {
  if (Inst.getOpcode() != RX::ADD_rri32)
    return false;
  MCOperand Rd = Inst.getOperand(0), Rs = Inst.getOperand(1);
  const MCOperand &ImmOp = Inst.getOperand(2);
  if (!ImmOp.isImm())
    return true;
  int64_t V = SignExtend64<32>(ImmOp.getImm());
  MCOperand Imm = MCOperand::createImm(V);
  if (Rd.getReg() == Rs.getReg() && V >= 0 && V <= 15)
    setOpcodeAndOperands(Inst, RX::ADD_ri4, {Rd, Rd, Imm});
  else if (isInt<8>(V))
    setOpcodeAndOperands(Inst, RX::ADD_rri8, {Rd, Rs, Imm});
  else if (isInt<16>(V))
    setOpcodeAndOperands(Inst, RX::ADD_rri16, {Rd, Rs, Imm});
  else if (isInt<24>(V))
    setOpcodeAndOperands(Inst, RX::ADD_rri24, {Rd, Rs, Imm});
  return true;
}

bool shrinkMem(MCInst &Inst) {
  for (const MemFamily &F : MemFamilies) {
    if (Inst.getOpcode() != F.D16)
      continue;
    // Loads: rd, base, dsp. Stores: rs, base, dsp.
    MCOperand Data = Inst.getOperand(0), Base = Inst.getOperand(1);
    int64_t Dsp = Inst.getOperand(2).getImm();
    int64_t Scaled = Dsp / F.Scale;
    if (Dsp == 0)
      setOpcodeAndOperands(Inst, F.D0, {Data, Base});
    else if (Scaled <= 31 && isLowReg(Data.getReg()) &&
             isLowReg(Base.getReg()))
      Inst.setOpcode(F.D5);
    else if (Scaled <= 255)
      Inst.setOpcode(F.D8);
    return true;
  }
  return false;
}

bool shrinkRRR(MCInst &Inst) {
  for (const RRRFamily &F : RRRFamilies) {
    if (Inst.getOpcode() != F.RRR)
      continue;
    // rd, rs, rs2: rd = rs2 op rs
    MCOperand Rd = Inst.getOperand(0), Rs = Inst.getOperand(1),
              Rs2 = Inst.getOperand(2);
    if (Rd.getReg() == Rs2.getReg())
      setOpcodeAndOperands(Inst, F.RR, {Rd, Rd, Rs});
    else if (F.Commutative && Rd.getReg() == Rs.getReg())
      setOpcodeAndOperands(Inst, F.RR, {Rd, Rd, Rs2});
    return true;
  }
  return false;
}

// shift #imm, Rs, Rd
bool shrinkShift(MCInst &Inst) {
  unsigned Opc;
  switch (Inst.getOpcode()) {
  case RX::SHLL_rri: Opc = RX::SHLL_ri; break;
  case RX::SHLR_rri: Opc = RX::SHLR_ri; break;
  case RX::SHAR_rri: Opc = RX::SHAR_ri; break;
  default:
    return false;
  }
  MCOperand Rd = Inst.getOperand(0), Imm = Inst.getOperand(1),
            Rs = Inst.getOperand(2);
  if (Rd.getReg() == Rs.getReg())
    setOpcodeAndOperands(Inst, Opc, {Rd, Rd, Imm});
  return true;
}

} // end anonymous namespace

void llvm::shrinkRXInst(MCInst &Inst) {
  shrinkImm(Inst) || shrinkAddImm(Inst) || shrinkMem(Inst) ||
      shrinkRRR(Inst) || shrinkShift(Inst);
}
