//===- RXMCTargetDesc.cpp - RX Target Descriptions --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides RX specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "RXMCTargetDesc.h"
#include "RXInstPrinter.h"
#include "RXMCAsmInfo.h"
#include "TargetInfo/RXTargetInfo.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "RXGenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "RXGenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "RXGenRegisterInfo.inc"

static MCInstrInfo *createRXMCInstrInfo() {
  auto *X = new MCInstrInfo();
  InitRXMCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createRXMCRegisterInfo(const Triple &TT) {
  auto *X = new MCRegisterInfo();
  // There is no link register; the return address lives on the stack.
  InitRXMCRegisterInfo(X, RX::R0);
  return X;
}

static MCSubtargetInfo *createRXMCSubtargetInfo(const Triple &TT,
                                                StringRef CPU, StringRef FS) {
  if (CPU.empty())
    CPU = "rxv3";
  return createRXMCSubtargetInfoImpl(TT, CPU, /*TuneCPU=*/CPU, FS);
}

static MCAsmInfo *createRXMCAsmInfo(const MCRegisterInfo &MRI,
                                    const Triple &TT,
                                    const MCTargetOptions &Options) {
  MCAsmInfo *MAI = new RXMCAsmInfo(TT, Options);

  // On entry the CFA is SP + 4 (the return address pushed by BSR/JSR).
  MCCFIInstruction Inst = MCCFIInstruction::cfiDefCfa(
      nullptr, MRI.getDwarfRegNum(RX::R0, true), 4);
  MAI->addInitialFrameState(Inst);

  return MAI;
}

static MCInstPrinter *createRXMCInstPrinter(const Triple &T,
                                            unsigned SyntaxVariant,
                                            const MCAsmInfo &MAI,
                                            const MCInstrInfo &MII,
                                            const MCRegisterInfo &MRI) {
  return new RXInstPrinter(MAI, MII, MRI);
}

namespace {

class RXMCInstrAnalysis : public MCInstrAnalysis {
public:
  explicit RXMCInstrAnalysis(const MCInstrInfo *Info)
      : MCInstrAnalysis(Info) {}

  // The target of a direct branch or call is its PC-relative operand, a
  // displacement from the start of the instruction. Indirect ones (jmp, jsr,
  // bra.l, bsr.l) have none.
  bool evaluateBranch(const MCInst &Inst, uint64_t Addr, uint64_t Size,
                      uint64_t &Target) const override {
    const MCInstrDesc &Desc = Info->get(Inst.getOpcode());
    if (!Desc.isBranch() && !Desc.isCall())
      return false;
    for (unsigned I = 0, E = Inst.getNumOperands(); I != E; ++I) {
      if (I >= Desc.getNumOperands() ||
          Desc.operands()[I].OperandType != MCOI::OPERAND_PCREL ||
          !Inst.getOperand(I).isImm())
        continue;
      Target = (Addr + Inst.getOperand(I).getImm()) & 0xffffffff;
      return true;
    }
    return false;
  }
};

} // end anonymous namespace

static MCInstrAnalysis *createRXMCInstrAnalysis(const MCInstrInfo *Info) {
  return new RXMCInstrAnalysis(Info);
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeRXTargetMC() {
  Target &TheRXTarget = getTheRXTarget();
  RegisterMCAsmInfoFn X(TheRXTarget, createRXMCAsmInfo);
  TargetRegistry::RegisterMCInstrInfo(TheRXTarget, createRXMCInstrInfo);
  TargetRegistry::RegisterMCRegInfo(TheRXTarget, createRXMCRegisterInfo);
  TargetRegistry::RegisterMCSubtargetInfo(TheRXTarget,
                                          createRXMCSubtargetInfo);
  TargetRegistry::RegisterMCInstPrinter(TheRXTarget, createRXMCInstPrinter);
  TargetRegistry::RegisterMCInstrAnalysis(TheRXTarget,
                                          createRXMCInstrAnalysis);
  TargetRegistry::RegisterMCCodeEmitter(TheRXTarget, createRXMCCodeEmitter);
  TargetRegistry::RegisterMCAsmBackend(TheRXTarget, createRXMCAsmBackend);
  TargetRegistry::RegisterObjectTargetStreamer(TheRXTarget,
                                               createRXObjectTargetStreamer);
}
