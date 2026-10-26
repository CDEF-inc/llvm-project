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
#include "RXSubtarget.h"
#include "TargetInfo/RXTargetInfo.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstBuilder.h"
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
    if (MI->getOpcode() == RX::CMPXCHG32) {
      emitCmpXchg(*MI);
      return;
    }
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

private:
  void emitCmpXchg(const MachineInstr &MI);
};

// CMPXCHG32 $dst, $tmp, $ptr, $cmp, $new. Both sequences are 16 bytes, the
// Size of the pseudo; the instructions are emitted as is (not shrunk).
//
// RXv2 and later:              RXv1 (interrupts masked):
//   1: movli [ptr], dst           mvfc   psw, tmp
//      cmp   cmp, dst             clrpsw i
//      bne.b 2f                   mov.l  [ptr], dst
//      mov.l new, tmp             cmp    cmp, dst
//      movco tmp, [ptr]           bne.b  2f
//      cmp   #0, tmp              mov.l  new, [ptr]
//      bne.b 1b                2: mvtc   tmp, psw
//   2:
//
// MOVCO fails (tmp = 1) when an interrupt or exception has been taken since
// the MOVLI. On RXv1 the sequence is atomic only in supervisor mode, where
// CLRPSW I takes effect.
void RXAsmPrinter::emitCmpXchg(const MachineInstr &MI) {
  MCRegister Dst = MI.getOperand(0).getReg();
  MCRegister Tmp = MI.getOperand(1).getReg();
  MCRegister Ptr = MI.getOperand(2).getReg();
  MCRegister Cmp = MI.getOperand(3).getReg();
  MCRegister New = MI.getOperand(4).getReg();
  MCSymbol *Done = OutContext.createTempSymbol();
  const MCExpr *DoneExpr = MCSymbolRefExpr::create(Done, OutContext);
  auto Emit = [&](const MCInst &Inst) { EmitToStreamer(*OutStreamer, Inst); };

  if (MI.getMF()->getSubtarget<RXSubtarget>().hasRXv2()) {
    MCSymbol *Loop = OutContext.createTempSymbol();
    OutStreamer->emitLabel(Loop);
    Emit(MCInstBuilder(RX::MOVLI).addReg(Dst).addReg(Ptr));
    Emit(MCInstBuilder(RX::CMP_rr).addReg(Dst).addReg(Cmp));
    Emit(MCInstBuilder(RX::BNE_B).addExpr(DoneExpr));
    Emit(MCInstBuilder(RX::MOV_L_rr).addReg(Tmp).addReg(New));
    Emit(MCInstBuilder(RX::MOVCO).addReg(Tmp).addReg(Tmp).addReg(Ptr));
    Emit(MCInstBuilder(RX::CMP_ri4).addReg(Tmp).addImm(0));
    Emit(MCInstBuilder(RX::BNE_B)
             .addExpr(MCSymbolRefExpr::create(Loop, OutContext)));
    OutStreamer->emitLabel(Done);
    return;
  }

  Emit(MCInstBuilder(RX::MVFC).addReg(Tmp).addReg(RX::PSW));
  Emit(MCInstBuilder(RX::CLRPSW_I));
  Emit(MCInstBuilder(RX::MOV_L_ld0).addReg(Dst).addReg(Ptr));
  Emit(MCInstBuilder(RX::CMP_rr).addReg(Dst).addReg(Cmp));
  Emit(MCInstBuilder(RX::BNE_B).addExpr(DoneExpr));
  Emit(MCInstBuilder(RX::MOV_L_st0).addReg(New).addReg(Ptr));
  OutStreamer->emitLabel(Done);
  Emit(MCInstBuilder(RX::MVTC_r).addReg(RX::PSW).addReg(Tmp));
}

} // end anonymous namespace

char RXAsmPrinter::ID = 0;

INITIALIZE_PASS(RXAsmPrinter, "rx-asm-printer", "RX Assembly Printer", false,
                false)

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeRXAsmPrinter() {
  RegisterAsmPrinter<RXAsmPrinter> X(getTheRXTarget());
}
