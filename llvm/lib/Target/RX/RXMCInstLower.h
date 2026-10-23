//===- RXMCInstLower.h - Lower MachineInstr to MCInst -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RX_RXMCINSTLOWER_H
#define LLVM_LIB_TARGET_RX_RXMCINSTLOWER_H

namespace llvm {

class AsmPrinter;
class MCContext;
class MCInst;
class MCOperand;
class MCSymbol;
class MachineInstr;
class MachineOperand;

/// Lowers MachineInstrs to MCInsts, expanding the BCC/SCC pseudos and
/// choosing the shortest encoding of each instruction (instruction selection
/// always uses the longest form).
class RXMCInstLower {
  MCContext &Ctx;
  AsmPrinter &Printer;

  MCOperand lowerSymbolOperand(const MachineOperand &MO, MCSymbol *Sym) const;

public:
  RXMCInstLower(MCContext &Ctx, AsmPrinter &Printer)
      : Ctx(Ctx), Printer(Printer) {}

  void lower(const MachineInstr *MI, MCInst &OutMI) const;
};

/// Rewrites an MCInst in place to the shortest encoding with the same
/// meaning (the same choice GNU as makes for the equivalent assembly).
void shrinkRXInst(MCInst &Inst);

} // end namespace llvm

#endif // LLVM_LIB_TARGET_RX_RXMCINSTLOWER_H
