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
  bool selectIndexedLoad(LoadSDNode *LD);
  bool selectIndexedStore(StoreSDNode *ST);

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

// The size of an [Rn+] / [-Rn] access and whether it moves up; false if the
// offset is not the access size.
static bool getIndexedMode(ISD::MemIndexedMode AM, SDValue Offset,
                           unsigned Size, bool &Inc) {
  auto *C = dyn_cast<ConstantSDNode>(Offset);
  if (!C || C->getZExtValue() != Size)
    return false;
  if (AM == ISD::POST_INC)
    Inc = true;
  else if (AM == ISD::PRE_DEC)
    Inc = false;
  else
    return false;
  return true;
}

// mov.sz / movu.sz [Rs+] / [-Rs], Rd: results Rd, the new Rs and the chain,
// as those of the indexed load.
bool RXDAGToDAGISel::selectIndexedLoad(LoadSDNode *LD) {
  unsigned Size = LD->getMemoryVT().getStoreSize();
  bool Inc;
  if (LD->getValueType(0) != MVT::i32 ||
      !getIndexedMode(LD->getAddressingMode(), LD->getOffset(), Size, Inc))
    return false;
  bool Sext = LD->getExtensionType() == ISD::SEXTLOAD;
  unsigned Opc;
  switch (Size) {
  case 1:
    Opc = Sext ? (Inc ? RX::MOV_B_ldpi : RX::MOV_B_ldpd)
               : (Inc ? RX::MOVU_B_ldpi : RX::MOVU_B_ldpd);
    break;
  case 2:
    Opc = Sext ? (Inc ? RX::MOV_W_ldpi : RX::MOV_W_ldpd)
               : (Inc ? RX::MOVU_W_ldpi : RX::MOVU_W_ldpd);
    break;
  case 4:
    Opc = Inc ? RX::MOV_L_ldpi : RX::MOV_L_ldpd;
    break;
  default:
    return false;
  }
  MachineSDNode *MN = CurDAG->getMachineNode(
      Opc, SDLoc(LD), MVT::i32, MVT::i32, MVT::Other,
      {LD->getBasePtr(), LD->getChain()});
  CurDAG->setNodeMemRefs(MN, {LD->getMemOperand()});
  ReplaceNode(LD, MN);
  return true;
}

// mov.sz Rs, [Rd+] / [-Rd]: results the new Rd and the chain, as those of
// the indexed store.
bool RXDAGToDAGISel::selectIndexedStore(StoreSDNode *ST) {
  unsigned Size = ST->getMemoryVT().getStoreSize();
  bool Inc;
  if (ST->getValue().getValueType() != MVT::i32 ||
      !getIndexedMode(ST->getAddressingMode(), ST->getOffset(), Size, Inc))
    return false;
  unsigned Opc;
  switch (Size) {
  case 1:
    Opc = Inc ? RX::MOV_B_stpi : RX::MOV_B_stpd;
    break;
  case 2:
    Opc = Inc ? RX::MOV_W_stpi : RX::MOV_W_stpd;
    break;
  case 4:
    Opc = Inc ? RX::MOV_L_stpi : RX::MOV_L_stpd;
    break;
  default:
    return false;
  }
  MachineSDNode *MN = CurDAG->getMachineNode(
      Opc, SDLoc(ST), MVT::i32, MVT::Other,
      {ST->getValue(), ST->getBasePtr(), ST->getChain()});
  CurDAG->setNodeMemRefs(MN, {ST->getMemOperand()});
  ReplaceNode(ST, MN);
  return true;
}

void RXDAGToDAGISel::Select(SDNode *Node) {
  if (Node->isMachineOpcode()) {
    Node->setNodeId(-1);
    return;
  }

  if (auto *LD = dyn_cast<LoadSDNode>(Node))
    if (LD->isIndexed() && selectIndexedLoad(LD))
      return;
  if (auto *ST = dyn_cast<StoreSDNode>(Node))
    if (ST->isIndexed() && selectIndexedStore(ST))
      return;

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
