//===- RXTargetMachine.cpp - Define TargetMachine for RX --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RXTargetMachine.h"
#include "RX.h"
#include "RXMachineFunctionInfo.h"
#include "TargetInfo/RXTargetInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/MC/TargetRegistry.h"
#include <optional>

using namespace llvm;

static Reloc::Model getRelocModel(std::optional<Reloc::Model> RM) {
  return RM.value_or(Reloc::Static);
}

RXTargetMachine::RXTargetMachine(const Target &T, const Triple &TT,
                                 StringRef CPU, StringRef FS,
                                 const TargetOptions &Options,
                                 std::optional<Reloc::Model> RM,
                                 std::optional<CodeModel::Model> CM,
                                 CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(T, TT.computeDataLayout(), TT,
                               CPU.empty() ? "rxv3" : CPU, FS, Options,
                               getRelocModel(RM),
                               getEffectiveCodeModel(CM, CodeModel::Small), OL),
      TLOF(std::make_unique<TargetLoweringObjectFileELF>()),
      Subtarget(TT, std::string(CPU), std::string(FS), *this) {
  initAsmInfo();
}

RXTargetMachine::~RXTargetMachine() = default;

namespace {

class RXPassConfig : public TargetPassConfig {
public:
  RXPassConfig(RXTargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  RXTargetMachine &getRXTargetMachine() const {
    return getTM<RXTargetMachine>();
  }

  void addIRPasses() override {
    addPass(createAtomicExpandLegacyPass());
    TargetPassConfig::addIRPasses();
  }

  bool addInstSelector() override {
    addPass(createRXISelDag(getRXTargetMachine(), getOptLevel()));
    return false;
  }

  // b<cond>.b only reaches +-128 bytes; relaxation inverts it around a bra.a.
  void addPreEmitPass() override { addPass(&BranchRelaxationPassID); }
};

} // end anonymous namespace

TargetPassConfig *RXTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new RXPassConfig(*this, PM);
}

MachineFunctionInfo *RXTargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return RXMachineFunctionInfo::create<RXMachineFunctionInfo>(Allocator, F,
                                                              STI);
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeRXTarget() {
  RegisterTargetMachine<RXTargetMachine> X(getTheRXTarget());
  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeRXAsmPrinterPass(PR);
  initializeRXDAGToDAGISelLegacyPass(PR);
}
