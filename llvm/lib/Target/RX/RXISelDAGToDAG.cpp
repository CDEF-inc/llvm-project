//===- RXISelDAGToDAG.cpp - A dag to dag inst selector for RX ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines an instruction selector for the RX target.
//
//===----------------------------------------------------------------------===//

#include "RX.h"
#include "RXSelectionDAGInfo.h"
#include "RXTargetMachine.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "rx-isel"
#define PASS_NAME "RX DAG->DAG Pattern Instruction Selection"

namespace {

class RXDAGToDAGISel : public SelectionDAGISel {
public:
  RXDAGToDAGISel() = delete;
  RXDAGToDAGISel(RXTargetMachine &TM, CodeGenOptLevel OptLevel)
      : SelectionDAGISel(TM, OptLevel) {}

  bool runOnMachineFunction(MachineFunction &MF) override {
    Subtarget = &MF.getSubtarget<RXSubtarget>();
    return SelectionDAGISel::runOnMachineFunction(MF);
  }

private:
  // For the pattern predicates (HasFPU, HasRXv2, ...).
  const RXSubtarget *Subtarget = nullptr;

#include "RXGenDAGISel.inc"

  void Select(SDNode *N) override;

  bool selectAddr(SDValue Addr, SDValue &Base, SDValue &Disp, unsigned Scale);
  bool SelectAddrB(SDValue Addr, SDValue &Base, SDValue &Disp) {
    return selectAddr(Addr, Base, Disp, 1);
  }
  bool SelectAddrW(SDValue Addr, SDValue &Base, SDValue &Disp) {
    return selectAddr(Addr, Base, Disp, 2);
  }
  bool SelectAddrL(SDValue Addr, SDValue &Base, SDValue &Disp) {
    return selectAddr(Addr, Base, Disp, 4);
  }
};

class RXDAGToDAGISelLegacy : public SelectionDAGISelLegacy {
public:
  static char ID;
  RXDAGToDAGISelLegacy(RXTargetMachine &TM, CodeGenOptLevel OptLevel)
      : SelectionDAGISelLegacy(ID,
                               std::make_unique<RXDAGToDAGISel>(TM, OptLevel)) {}
};

} // end anonymous namespace

char RXDAGToDAGISelLegacy::ID;

INITIALIZE_PASS(RXDAGToDAGISelLegacy, DEBUG_TYPE, PASS_NAME, false, false)

FunctionPass *llvm::createRXISelDag(RXTargetMachine &TM,
                                    CodeGenOptLevel OptLevel) {
  return new RXDAGToDAGISelLegacy(TM, OptLevel);
}

// Base register (or frame index) plus a displacement that is a multiple of
// Scale and fits the dsp:16 field. Frame index offsets are checked again in
// eliminateFrameIndex.
bool RXDAGToDAGISel::selectAddr(SDValue Addr, SDValue &Base, SDValue &Disp,
                                unsigned Scale) {
  SDLoc DL(Addr);
  int64_t Offset = 0;

  if (CurDAG->isBaseWithConstantOffset(Addr)) {
    int64_t C = cast<ConstantSDNode>(Addr.getOperand(1))->getSExtValue();
    if (C >= 0 && C % Scale == 0 && C / Scale <= 0xffff) {
      Offset = C;
      Addr = Addr.getOperand(0);
    }
  }

  if (auto *FIN = dyn_cast<FrameIndexSDNode>(Addr))
    Base = CurDAG->getTargetFrameIndex(FIN->getIndex(), MVT::i32);
  else
    Base = Addr;
  Disp = CurDAG->getTargetConstant(Offset, DL, MVT::i32);
  return true;
}

void RXDAGToDAGISel::Select(SDNode *Node) {
  if (Node->isMachineOpcode()) {
    Node->setNodeId(-1);
    return;
  }

  SDLoc DL(Node);
  if (Node->getOpcode() == ISD::FrameIndex) {
    int FI = cast<FrameIndexSDNode>(Node)->getIndex();
    SDValue TFI = CurDAG->getTargetFrameIndex(FI, MVT::i32);
    CurDAG->SelectNodeTo(Node, RX::FRMIDX, MVT::i32, TFI,
                         CurDAG->getTargetConstant(0, DL, MVT::i32));
    return;
  }

  SelectCode(Node);
}
