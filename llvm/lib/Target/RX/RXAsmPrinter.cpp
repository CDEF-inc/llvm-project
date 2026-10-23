//===- RXAsmPrinter.cpp - RX LLVM assembly writer ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a printer that converts from our internal representation
// of machine-dependent LLVM code to RX machine code.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/RXInstPrinter.h"
#include "RX.h"
#include "RXMCInstLower.h"
#include "TargetInfo/RXTargetInfo.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

namespace {

class RXAsmPrinter : public AsmPrinter {
public:
  static char ID;

  RXAsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer), ID) {}

  StringRef getPassName() const override { return "RX Assembly Printer"; }

  void emitInstruction(const MachineInstr *MI) override {
    RXMCInstLower Lower(OutContext, *this);
    MCInst Inst;
    Lower.lower(MI, Inst);
    shrinkRXInst(Inst);
    EmitToStreamer(*OutStreamer, Inst);
  }

  // Inline assembly operands: registers by name (r1), immediates as plain
  // numbers, symbols by name.
  bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                       const char *ExtraCode, raw_ostream &O) override {
    if (ExtraCode && ExtraCode[0])
      return AsmPrinter::PrintAsmOperand(MI, OpNo, ExtraCode, O);
    const MachineOperand &MO = MI->getOperand(OpNo);
    switch (MO.getType()) {
    case MachineOperand::MO_Register:
      O << RXInstPrinter::getRegisterName(MO.getReg());
      return false;
    case MachineOperand::MO_Immediate:
      O << MO.getImm();
      return false;
    case MachineOperand::MO_GlobalAddress:
      PrintSymbolOperand(MO, O);
      return false;
    default:
      return true;
    }
  }

  // "m" operands: [Rn].
  bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                             const char *ExtraCode, raw_ostream &O) override {
    if (ExtraCode && ExtraCode[0])
      return true;
    const MachineOperand &MO = MI->getOperand(OpNo);
    if (!MO.isReg())
      return true;
    O << '[' << RXInstPrinter::getRegisterName(MO.getReg()) << ']';
    return false;
  }
};

} // end anonymous namespace

char RXAsmPrinter::ID = 0;

INITIALIZE_PASS(RXAsmPrinter, "rx-asm-printer", "RX Assembly Printer", false,
                false)

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeRXAsmPrinter() {
  RegisterAsmPrinter<RXAsmPrinter> X(getTheRXTarget());
}
