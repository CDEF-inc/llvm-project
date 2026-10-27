//===- RXSelectionDAGInfo.h - RX SelectionDAG Info --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RX_RXSELECTIONDAGINFO_H
#define LLVM_LIB_TARGET_RX_RXSELECTIONDAGINFO_H

#include "llvm/CodeGen/SelectionDAGTargetInfo.h"

#define GET_SDNODE_ENUM
#include "RXGenSDNodeInfo.inc"

namespace llvm {

class RXSelectionDAGInfo : public SelectionDAGGenTargetInfo {
public:
  RXSelectionDAGInfo();
  ~RXSelectionDAGInfo() override;

  // memcpy / memset that are not expanded to loads and stores (a variable
  // or large size) use the string instructions instead of a call.
  SDValue EmitTargetCodeForMemcpy(SelectionDAG &DAG, const SDLoc &DL,
                                  SDValue Chain, SDValue Dst, SDValue Src,
                                  SDValue Size, Align DstAlign,
                                  Align SrcAlign, bool IsVolatile,
                                  bool AlwaysInline,
                                  MachinePointerInfo DstPtrInfo,
                                  MachinePointerInfo SrcPtrInfo) const override;
  SDValue EmitTargetCodeForMemset(SelectionDAG &DAG, const SDLoc &DL,
                                  SDValue Chain, SDValue Dst, SDValue Val,
                                  SDValue Size, Align Alignment,
                                  bool IsVolatile, bool AlwaysInline,
                                  MachinePointerInfo DstPtrInfo) const override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_RX_RXSELECTIONDAGINFO_H
