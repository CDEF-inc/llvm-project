//===- RXAsmBackend.cpp - RX Assembler Backend ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/RXFixupKinds.h"
#include "MCTargetDesc/RXMCTargetDesc.h"
#include "MCTargetDesc/RXSizelessBranch.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

namespace {

// R_RX_RH_RELAX addends (GNU's include/elf/rx.h): what may shrink, and how
// many relocations of the instruction follow.
enum : unsigned {
  RelaxImm12 = 0x20,       // an immediate whose size field is at bit 12
  RelaxBra = 0x200,        // a branch (decoded by the linker)
  RelaxAlign = 0x10000000, // alignment; the low bits are the alignment
};

class RXAsmBackend : public MCAsmBackend {
  // -mattr=+relax: hints and relocations for linker relaxation.
  bool Relax;

public:
  explicit RXAsmBackend(const MCSubtargetInfo &STI)
      : MCAsmBackend(llvm::endianness::little),
        Relax(STI.hasFeature(RX::FeatureRelax)) {}

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createRXELFObjectWriter(ELF::ELFOSABI_NONE, Relax);
  }

  // With relaxation, label differences across code the linker may shorten
  // are relocations; these encode the line table's and the CFI's deltas so
  // that they can take one.
  bool relaxDwarfLineAddr(MCFragment &F) const override;
  bool relaxDwarfCFA(MCFragment &F) const override;
  bool relaxAlign(MCFragment &F, unsigned &Size) override;

  MCFixupKindInfo getFixupKindInfo(MCFixupKind Kind) const override {
    // clang-format off
    static const MCFixupKindInfo Infos[RX::NumTargetFixupKinds] = {
      // name                offset bits flags
      {"fixup_rx_pcrel3",    0,     3,   0},
      {"fixup_rx_pcrel8",    0,     8,   0},
      {"fixup_rx_pcrel16",   0,     16,  0},
      {"fixup_rx_pcrel24",   0,     24,  0},
    };
    // clang-format on
    static_assert(std::size(Infos) == RX::NumTargetFixupKinds,
                  "Not all fixup kinds added to Infos array");

    if (Kind < FirstTargetFixupKind)
      return MCAsmBackend::getFixupKindInfo(Kind);
    return Infos[Kind - FirstTargetFixupKind];
  }

  void applyFixup(const MCFragment &F, const MCFixup &Fixup,
                  const MCValue &Target, uint8_t *Data, uint64_t Value,
                  bool IsResolved) override;

  // Branches written without a size (BR_SIZELESS) step through their forms
  // until the target is in range (see RXSizelessBranch.h).
  bool mayNeedRelaxation(unsigned Opcode, ArrayRef<MCOperand> Operands,
                         const MCSubtargetInfo &STI) const override {
    return Opcode == RX::BR_SIZELESS &&
           Operands[2].getImm() + 1 <
               int64_t(RX::getSizelessBranchForms(Operands[1].getImm()).size());
  }

  bool fixupNeedsRelaxationAdvanced(const MCFragment &F, const MCFixup &Fixup,
                                    const MCValue &Target, uint64_t Value,
                                    bool Resolved) const override;

  void relaxInstruction(MCInst &Inst,
                        const MCSubtargetInfo &STI) const override {
    Inst.getOperand(2).setImm(Inst.getOperand(2).getImm() + 1);
  }

  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *STI) const override {
    // NOP is the single byte 0x03.
    for (uint64_t I = 0; I != Count; ++I)
      OS << char(0x03);
    return true;
  }

private:
  void recordRelaxHint(const MCFragment &F, const MCFixup &Fixup);
  bool isPCRelFixupResolved(const MCSymbol *Sym, const MCFragment &F);

  // A temporary symbol at the fixup's fragment, for isPCRelFixupResolved.
  MCSymbol *PCRelTemp = nullptr;
};

} // end anonymous namespace

bool RXAsmBackend::fixupNeedsRelaxationAdvanced(const MCFragment &F,
                                                const MCFixup &Fixup,
                                                const MCValue &Target,
                                                uint64_t Value,
                                                bool Resolved) const {
  ArrayRef<MCOperand> Ops = F.getOperands();
  unsigned Kind = Ops[1].getImm(), Step = Ops[2].getImm();
  if (!Resolved) {
    // In a section the linker may shorten (relaxation), a target in the
    // same section is unresolved because of that. Its distance is known, and
    // the linker only deletes bytes, so a form that reaches it now still
    // does. (Without relaxation, as with GNU as, a global symbol in the same
    // section is also unresolved and gets the fixed form.)
    const MCSymbol *Sym = Target.getAddSym();
    if (!F.getParent()->isLinkerRelaxable() || !Sym || Target.getSubSym() ||
        !Sym->isInSection() || &Sym->getSection() != F.getParent())
      // GNU as gives a target it cannot resolve a fixed form.
      return Step < RX::getSizelessBranchUnresolvedStep(Kind);
    Value = Asm->getSymbolOffset(*Sym) + Target.getConstant() -
            (Asm->getFragmentOffset(F) + Fixup.getOffset());
  }
  // Value is relative to the fixup. Like GNU as, measure the distance from
  // the start of the whole sequence: for a single branch that is the
  // branch itself, for b<!cond> + bra.w the b<!cond> (so a bra.w that would
  // just reach becomes a bra.a, as in GNU as). The sequence is the
  // fragment's variable part; the fixup's offset also counts the fixed part
  // before it.
  int64_t Dsp = static_cast<int64_t>(Value) +
                (Fixup.getOffset() - F.getFixedSize());
  switch (Fixup.getKind()) {
  case RX::fixup_rx_pcrel3:
    return Dsp < 3 || Dsp > 10;
  case RX::fixup_rx_pcrel8:
    return !isInt<8>(Dsp);
  case RX::fixup_rx_pcrel16:
    return !isInt<16>(Dsp);
  default:
    return false;
  }
}

void RXAsmBackend::applyFixup(const MCFragment &F, const MCFixup &Fixup,
                              const MCValue &Target, uint8_t *Data,
                              uint64_t Value, bool IsResolved) {
  // A - B + C with B not resolvable here: like GNU as, the stack relocations
  // R_RX_SYM A+C, R_RX_SYM B, R_RX_OPsub and R_RX_ABS32 / ABS16 / ABS8 (the
  // field stays zero). Instructions take such a value as a 32-bit immediate;
  // with relaxation the DWARF deltas in data are 8, 16 or 32 bits.
  if (!IsResolved && Target.getSubSym()) {
    unsigned Abs;
    switch (Fixup.getKind()) {
    case FK_Data_1:
      Abs = ELF::R_RX_ABS8;
      break;
    case FK_Data_2:
      Abs = ELF::R_RX_ABS16;
      break;
    case FK_Data_4:
      Abs = ELF::R_RX_ABS32;
      break;
    default:
      getContext().reportError(Fixup.getLoc(),
                               "a difference of symbols needs an 8, 16 or "
                               "32-bit field");
      return;
    }
    MCValue A =
        MCValue::get(Target.getAddSym(), nullptr, Target.getConstant());
    MCValue B = MCValue::get(Target.getSubSym());
    auto Record = [&](unsigned Type, const MCValue &V) {
      MCFixup R = MCFixup::create(Fixup.getOffset(), nullptr,
                                  FirstLiteralRelocationKind + Type);
      uint64_t FixedValue = 0;
      Asm->getWriter().recordRelocation(F, R, V, FixedValue);
    };
    Record(ELF::R_RX_SYM, A);
    Record(ELF::R_RX_SYM, B);
    Record(ELF::R_RX_OPsub, B);
    Record(Abs, B);
    return;
  }
  // A branch over code that the linker may shorten needs a relocation.
  if (IsResolved && Fixup.isPCRel())
    IsResolved = isPCRelFixupResolved(Target.getAddSym(), F);
  if (!IsResolved && Fixup.isLinkerRelaxable())
    recordRelaxHint(F, Fixup);
  maybeAddReloc(F, Fixup, Target, Value, IsResolved);
  // Unresolved fields stay zero; the RELA addend carries the value.
  if (!IsResolved)
    return;

  MCContext &Ctx = getContext();
  unsigned Kind = Fixup.getKind();
  unsigned NumBytes;
  switch (Kind) {
  case RX::fixup_rx_pcrel3: {
    // The field is in the opcode byte, so Value is already relative to the
    // start of the instruction.
    int64_t Dsp = static_cast<int64_t>(Value);
    if (Dsp < 3 || Dsp > 10)
      Ctx.reportError(Fixup.getLoc(), "branch target out of range for .S "
                                      "(3 to 10 bytes)");
    Data[0] |= uint8_t(Dsp & 7);
    return;
  }
  case RX::fixup_rx_pcrel8:
  case RX::fixup_rx_pcrel16:
  case RX::fixup_rx_pcrel24: {
    // The field is one byte into the instruction: rebase on the opcode.
    int64_t Dsp = static_cast<int64_t>(Value) + 1;
    unsigned Bits = Kind == RX::fixup_rx_pcrel8    ? 8
                    : Kind == RX::fixup_rx_pcrel16 ? 16
                                                   : 24;
    if (!isIntN(Bits, Dsp))
      Ctx.reportError(Fixup.getLoc(), "branch target out of range");
    Value = static_cast<uint64_t>(Dsp);
    NumBytes = Bits / 8;
    break;
  }
  case FK_Data_1:
    NumBytes = 1;
    break;
  case FK_Data_2:
    NumBytes = 2;
    break;
  case FK_Data_4:
    NumBytes = 4;
    break;
  default:
    llvm_unreachable("unknown fixup kind");
  }

  for (unsigned I = 0; I != NumBytes; ++I)
    Data[I] |= uint8_t((Value >> (I * 8)) & 0xff);
}

// Whether a PC-relative fixup to SYM in fragment F is resolved: unless
// linker-relaxable fragments (instructions the linker may shorten, or
// alignment padding it may delete) lie between them (as for RISC-V).
bool RXAsmBackend::isPCRelFixupResolved(const MCSymbol *Sym,
                                        const MCFragment &F) {
  if (!Sym || !F.getParent()->isLinkerRelaxable())
    return true;
  if (!PCRelTemp)
    PCRelTemp = getContext().createTempSymbol();
  PCRelTemp->setFragment(const_cast<MCFragment *>(&F));
  MCValue Res;
  MCExpr::evaluateSymbolicAdd(Asm, false, MCValue::get(Sym),
                              MCValue::get(nullptr, PCRelTemp), Res);
  return !Res.getSubSym();
}

// GNU as -relax: an R_RX_RH_RELAX at the start of the instruction, before its
// relocation, its addend saying what the linker may shorten.
void RXAsmBackend::recordRelaxHint(const MCFragment &F, const MCFixup &Fixup) {
  unsigned Start, Flags;
  if (Fixup.getKind() == RX::fixup_rx_pcrel24) {
    // bsr.a / bra.a: the displacement is one byte into the instruction.
    Start = Fixup.getOffset() - 1;
    Flags = RelaxBra;
  } else {
    // mov.l #imm32, rd: fb, rd:li:10, imm32; li is at bit 12.
    assert(Fixup.getKind() == FK_Data_4 && "unexpected relaxable fixup");
    Start = Fixup.getOffset() - 2;
    Flags = RelaxImm12;
  }
  MCFixup Hint = MCFixup::create(Start, nullptr,
                                 FirstLiteralRelocationKind + ELF::R_RX_RH_RELAX);
  uint64_t FixedValue = 0;
  Asm->getWriter().recordRelocation(
      F, Hint, MCValue::get(nullptr, nullptr, Flags | 1), FixedValue);
}

// A line delta across shortenable code: DW_LNS_fixed_advance_pc with a 16-bit
// operand that a relocation fills (DW_LNE_set_address beyond that), as GNU
// as -relax writes it.
bool RXAsmBackend::relaxDwarfLineAddr(MCFragment &F) const {
  int64_t LineDelta = F.getDwarfLineDelta();
  const MCExpr &AddrDelta = F.getDwarfAddrDelta();
  int64_t Value;
  if (AddrDelta.evaluateAsAbsolute(Value, *Asm))
    return false;
  [[maybe_unused]] bool IsAbsolute =
      AddrDelta.evaluateKnownAbsolute(Value, *Asm);
  assert(IsAbsolute && "line delta with an invalid expression");

  SmallVector<char> Data;
  raw_svector_ostream OS(Data);
  // INT64_MAX is a signal that this is actually a DW_LNE_end_sequence.
  if (LineDelta != INT64_MAX) {
    OS << uint8_t(dwarf::DW_LNS_advance_line);
    encodeSLEB128(LineDelta, OS);
  }
  MCFixup Fixup;
  if (isUInt<16>(Value)) {
    OS << uint8_t(dwarf::DW_LNS_fixed_advance_pc);
    Fixup = MCFixup::create(OS.tell(), &AddrDelta, FK_Data_2);
    support::endian::write<uint16_t>(OS, 0, llvm::endianness::little);
  } else {
    // The end label's address.
    OS << uint8_t(dwarf::DW_LNS_extended_op) << uint8_t(5)
       << uint8_t(dwarf::DW_LNE_set_address);
    Fixup = MCFixup::create(OS.tell(), cast<MCBinaryExpr>(AddrDelta).getLHS(),
                            FK_Data_4);
    support::endian::write<uint32_t>(OS, 0, llvm::endianness::little);
  }
  if (LineDelta == INT64_MAX)
    OS << uint8_t(dwarf::DW_LNS_extended_op) << uint8_t(1)
       << uint8_t(dwarf::DW_LNE_end_sequence);
  else
    OS << uint8_t(dwarf::DW_LNS_copy);
  F.setVarContents(Data);
  F.setVarFixups({Fixup});
  return true;
}

// A CFI advance across shortenable code: DW_CFA_advance_loc1 / 2 / 4 with an
// operand that a relocation fills (the 6-bit DW_CFA_advance_loc shares its
// byte with the opcode).
bool RXAsmBackend::relaxDwarfCFA(MCFragment &F) const {
  const MCExpr &AddrDelta = F.getDwarfAddrDelta();
  int64_t Value;
  if (AddrDelta.evaluateAsAbsolute(Value, *Asm))
    return false;
  [[maybe_unused]] bool IsAbsolute =
      AddrDelta.evaluateKnownAbsolute(Value, *Asm);
  assert(IsAbsolute && "CFA with invalid expression");
  if (Value == 0) {
    F.clearVarContents();
    F.clearVarFixups();
    return true;
  }
  SmallVector<char, 8> Data;
  raw_svector_ostream OS(Data);
  MCFixupKind Kind;
  if (isUInt<8>(Value)) {
    OS << uint8_t(dwarf::DW_CFA_advance_loc1) << uint8_t(0);
    Kind = FK_Data_1;
  } else if (isUInt<16>(Value)) {
    OS << uint8_t(dwarf::DW_CFA_advance_loc2);
    support::endian::write<uint16_t>(OS, 0, llvm::endianness::little);
    Kind = FK_Data_2;
  } else {
    OS << uint8_t(dwarf::DW_CFA_advance_loc4);
    support::endian::write<uint32_t>(OS, 0, llvm::endianness::little);
    Kind = FK_Data_4;
  }
  F.setVarContents(Data);
  F.setVarFixups({MCFixup::create(1, &AddrDelta, Kind)});
  return true;
}

// Alignment after code the linker may shorten: the most padding it can need
// (alignment - 1 NOPs) and an R_RX_RH_RELAX at its start whose addend is
// RX_RELAXA_ALIGN | alignment; the linker deletes the NOPs not needed. (GNU
// as instead puts the hint after the padding it computed.)
bool RXAsmBackend::relaxAlign(MCFragment &F, unsigned &Size) {
  auto *Sec = F.getParent();
  if (!Relax || F.getLayoutOrder() <= Sec->firstLinkerRelaxable() ||
      F.getAlignment() <= 1)
    return false;
  Size = F.getAlignment().value() - 1;
  auto *Expr = MCConstantExpr::create(RelaxAlign | F.getAlignment().value(),
                                      getContext());
  F.setVarFixups({MCFixup::create(
      0, Expr, FirstLiteralRelocationKind + ELF::R_RX_RH_RELAX)});
  F.setLinkerRelaxable();
  return true;
}

MCAsmBackend *llvm::createRXMCAsmBackend(const Target &T,
                                         const MCSubtargetInfo &STI,
                                         const MCRegisterInfo &MRI,
                                         const MCTargetOptions &Options) {
  return new RXAsmBackend(STI);
}
