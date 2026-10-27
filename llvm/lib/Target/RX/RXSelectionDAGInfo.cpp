//===- RXSelectionDAGInfo.cpp - RX SelectionDAG Info ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RXSelectionDAGInfo.h"
#include "MCTargetDesc/RXMCTargetDesc.h"
#include "llvm/CodeGen/SelectionDAG.h"

#define GET_SDNODE_DESC
#include "RXGenSDNodeInfo.inc"

using namespace llvm;

RXSelectionDAGInfo::RXSelectionDAGInfo()
    : SelectionDAGGenTargetInfo(RXGenSDNodeInfo) {}

RXSelectionDAGInfo::~RXSelectionDAGInfo() = default;

// A string instruction: R1 = destination, R2 = source or value, R3 = count
// (it clobbers them; see the Defs in RXInstrInfo.td).
static SDValue emitStringInsn(SelectionDAG &DAG, const SDLoc &DL,
                              unsigned Opc, SDValue Chain, SDValue R1,
                              SDValue R2, SDValue R3) {
  SDValue Glue;
  for (auto [Reg, Val] :
       {std::pair(RX::R1, R1), std::pair(RX::R2, R2), std::pair(RX::R3, R3)}) {
    Chain = DAG.getCopyToReg(Chain, DL, Reg, Val, Glue);
    Glue = Chain.getValue(1);
  }
  return SDValue(
      DAG.getMachineNode(Opc, DL, DAG.getVTList(MVT::Other), {Chain, Glue}),
      0);
}

// smovf copies R3 bytes from [R2] to [R1] upwards.
SDValue RXSelectionDAGInfo::EmitTargetCodeForMemcpy(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Dst,
    SDValue Src, SDValue Size, Align DstAlign, Align SrcAlign,
    bool IsVolatile, bool AlwaysInline, MachinePointerInfo DstPtrInfo,
    MachinePointerInfo SrcPtrInfo) const {
  return emitStringInsn(DAG, DL, RX::SMOVF, Chain, Dst, Src,
                        DAG.getZExtOrTrunc(Size, DL, MVT::i32));
}

// sstr.b stores the low byte of R2 R3 times from [R1]; a constant value over
// a size that is a multiple of 4, 4-byte aligned, is stored by sstr.l.
SDValue RXSelectionDAGInfo::EmitTargetCodeForMemset(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Dst,
    SDValue Val, SDValue Size, Align Alignment, bool IsVolatile,
    bool AlwaysInline, MachinePointerInfo DstPtrInfo) const {
  auto *CVal = dyn_cast<ConstantSDNode>(Val);
  auto *CSize = dyn_cast<ConstantSDNode>(Size);
  if (CVal && CSize && CSize->getZExtValue() % 4 == 0 && Alignment >= 4) {
    uint32_t Byte = CVal->getZExtValue() & 0xff;
    return emitStringInsn(DAG, DL, RX::SSTR_L, Chain, Dst,
                          DAG.getConstant(Byte * 0x01010101, DL, MVT::i32),
                          DAG.getConstant(CSize->getZExtValue() / 4, DL,
                                          MVT::i32));
  }
  return emitStringInsn(DAG, DL, RX::SSTR_B, Chain, Dst,
                        DAG.getAnyExtOrTrunc(Val, DL, MVT::i32),
                        DAG.getZExtOrTrunc(Size, DL, MVT::i32));
}
