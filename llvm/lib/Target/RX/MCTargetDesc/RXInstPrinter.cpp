//===- RXInstPrinter.cpp - Convert RX MCInst to assembly syntax -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class prints an RX MCInst to a .s file.
//
//===----------------------------------------------------------------------===//

#include "RXInstPrinter.h"
#include "MCTargetDesc/RXMCTargetDesc.h"
#include "MCTargetDesc/RXSizelessBranch.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

#include "RXGenAsmWriter.inc"

void RXInstPrinter::printRegName(raw_ostream &OS, MCRegister Reg) {
  OS << getRegisterName(Reg);
}

void RXInstPrinter::printInst(const MCInst *MI, uint64_t Address,
                              StringRef Annot, const MCSubtargetInfo &STI,
                              raw_ostream &O) {
  if (MI->getOpcode() == RX::BR_SIZELESS) {
    // Printed as written, without a size; the assembler picks one again.
    static const char *const Conds[] = {"eq", "ne", "geu", "ltu", "gtu",
                                        "leu", "pz", "n",  "ge",  "lt",
                                        "gt", "le", "o",  "no"};
    unsigned Kind = MI->getOperand(1).getImm();
    O << '\t';
    if (Kind == RX::BrKindBra)
      O << "bra";
    else if (Kind == RX::BrKindBsr)
      O << "bsr";
    else
      O << 'b' << Conds[Kind - RX::BrKindCond];
    O << '\t';
    printPCRel(MI, 0, O);
    printAnnotation(O, Annot);
    return;
  }
  printInstruction(MI, Address, O);
  printAnnotation(O, Annot);
}

void RXInstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                 raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isReg()) {
    printRegName(O, Op.getReg());
    return;
  }
  if (Op.isImm()) {
    O << '#' << Op.getImm();
    return;
  }
  assert(Op.isExpr() && "unknown operand kind in printOperand");
  O << '#';
  MAI.printExpr(O, *Op.getExpr());
}

void RXInstPrinter::printDisp(const MCInst *MI, unsigned OpNo,
                              raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isImm())
    O << Op.getImm();
  else
    MAI.printExpr(O, *Op.getExpr());
}

void RXInstPrinter::printPCRel(const MCInst *MI, unsigned OpNo,
                               raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isImm())
    O << Op.getImm();
  else
    MAI.printExpr(O, *Op.getExpr());
}

void RXInstPrinter::printPCRel(const MCInst *MI, uint64_t Address,
                               unsigned OpNo, raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isImm() && PrintBranchImmAsAddress) {
    // Displacements are from the start of the branch instruction.
    O << formatHex((Address + Op.getImm()) & 0xffffffff);
    return;
  }
  printPCRel(MI, OpNo, O);
}
