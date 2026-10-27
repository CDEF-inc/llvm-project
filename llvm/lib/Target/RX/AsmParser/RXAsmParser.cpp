//===- RXAsmParser.cpp - Parse RX assembly to MCInst instructions ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Accepts GNU as (rx-elf) syntax:
//   #imm          immediate
//   dsp[Rn] [Rn]  register indirect with optional displacement (in bytes)
//   [Rn+] [-Rn]   post-increment / pre-decrement
//   [Ri, Rb]      register-indexed (Rb + Ri * size)
//   Rlo-Rhi       register range (PUSHM/POPM/RTSD)
//   label / expr  branch target
//
// Where an instruction has several encodings, the operand classes in
// RXInstrInfo.td make the matcher choose the same (shortest) one as GNU as.
//
// Like GNU as, mov / push / sstr / suntil / swhile / rmpa without a size
// are .L and movu is .W; bra / bsr Rn are .L; and bra / bsr / b<cond> to a
// label without a size get the form the distance needs (BR_SIZELESS).
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/RXMCTargetDesc.h"
#include "MCTargetDesc/RXSizelessBranch.h"
#include "TargetInfo/RXTargetInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCParser/AsmLexer.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"

#define DEBUG_TYPE "rx-asm-parser"

using namespace llvm;

namespace {

class RXAsmParser : public MCTargetAsmParser {
  bool matchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override;

  bool parseRegister(MCRegister &Reg, SMLoc &StartLoc, SMLoc &EndLoc) override;
  ParseStatus tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                               SMLoc &EndLoc) override;

  bool parseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override;

  bool parseOperand(OperandVector &Operands);
  bool parseBracketedRegister(OperandVector &Operands);
  void rewriteSubImmediate(OperandVector &Operands);
  bool parseSizelessBranch(StringRef Name, SMLoc NameLoc,
                           OperandVector &Operands);

  // The kind (see RXSizelessBranch.h) of a branch to a label written without
  // a size, set by parseInstruction for matchAndEmitInstruction.
  std::optional<unsigned> SizelessBranchKind;

#define GET_ASSEMBLER_HEADER
#include "RXGenAsmMatcher.inc"

public:
  RXAsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser,
              const MCInstrInfo &MII)
      : MCTargetAsmParser(STI, MII) {
    MCAsmParserExtension::Initialize(Parser);
    setAvailableFeatures(ComputeAvailableFeatures(STI.getFeatureBits()));
  }
};

class RXOperand : public MCParsedAsmOperand {
  enum KindTy { k_Tok, k_Reg, k_Imm } Kind;

  StringRef Tok;
  MCRegister Reg;
  const MCExpr *Imm = nullptr;
  // True for "#expr" (an immediate), false for a bare expression (a
  // displacement or a branch target).
  bool HasHash = false;
  SMLoc Start, End;

  bool getConstant(int64_t &V) const {
    return Kind == k_Imm && Imm->evaluateAsAbsolute(V);
  }

  // GNU as treats immediates modulo 2^32: 0x80000000-0xffffffff are the
  // negative values. Anything outside [-2^31, 2^32) does not fit.
  bool getImm32(int64_t &V) const {
    if (!getConstant(V) || V < INT32_MIN || V > UINT32_MAX)
      return false;
    if (V > INT32_MAX)
      V -= int64_t(1) << 32;
    return true;
  }

  bool isHashImmInRange(int64_t Lo, int64_t Hi) const {
    int64_t V;
    return Kind == k_Imm && HasHash && getConstant(V) && V >= Lo && V <= Hi;
  }

  bool isSignedImm(unsigned Bits) const {
    int64_t V;
    return Kind == k_Imm && HasHash && getImm32(V) && isIntN(Bits, V);
  }

  bool isDsp(int64_t Max, unsigned Scale) const {
    int64_t V;
    return Kind == k_Imm && !HasHash && getConstant(V) && V >= 0 &&
           V <= Max * Scale && V % Scale == 0;
  }

public:
  RXOperand(KindTy K, SMLoc S, SMLoc E) : Kind(K), Start(S), End(E) {}

  static std::unique_ptr<RXOperand> createToken(StringRef Str, SMLoc S) {
    auto Op = std::make_unique<RXOperand>(k_Tok, S, S);
    Op->Tok = Str;
    return Op;
  }

  static std::unique_ptr<RXOperand> createReg(MCRegister Reg, SMLoc S,
                                              SMLoc E) {
    auto Op = std::make_unique<RXOperand>(k_Reg, S, E);
    Op->Reg = Reg;
    return Op;
  }

  static std::unique_ptr<RXOperand> createImm(const MCExpr *Val, bool HasHash,
                                              SMLoc S, SMLoc E) {
    auto Op = std::make_unique<RXOperand>(k_Imm, S, E);
    Op->Imm = Val;
    Op->HasHash = HasHash;
    return Op;
  }

  bool isToken() const override { return Kind == k_Tok; }
  bool isReg() const override { return Kind == k_Reg; }
  bool isImm() const override { return Kind == k_Imm; }
  bool isMem() const override { return false; }
  bool isHashImm() const { return Kind == k_Imm && HasHash; }

  StringRef getToken() const {
    assert(Kind == k_Tok && "not a token");
    return Tok;
  }
  MCRegister getReg() const override {
    assert(Kind == k_Reg && "not a register");
    return Reg;
  }
  const MCExpr *getImm() const {
    assert(Kind == k_Imm && "not an immediate");
    return Imm;
  }
  SMLoc getStartLoc() const override { return Start; }
  SMLoc getEndLoc() const override { return End; }

  // Immediate classes (see RXInstrInfo.td). U4/U5/U8 compare the value as
  // written, like rx_uintop in GNU as; the signed classes use 32-bit wrap.
  bool isImmU4() const { return isHashImmInRange(0, 15); }
  bool isImmU5() const { return isHashImmInRange(0, 31); }
  bool isImmU8() const { return isHashImmInRange(0, 255); }
  bool isImmS8() const { return isSignedImm(8); }
  bool isImmS8Neg() const {
    int64_t V;
    return isSignedImm(8) && getImm32(V) && V < 0;
  }
  bool isImmS16() const { return isSignedImm(16); }
  bool isImmS24() const { return isSignedImm(24); }
  bool isImmB8() const { return isHashImmInRange(-128, 255); }
  bool isImmW16() const { return isHashImmInRange(-32768, 65535); }
  bool isImmW8() const {
    int64_t V;
    return isImmW16() && getConstant(V) && isInt<8>(int16_t(V));
  }
  bool isImmS32() const {
    int64_t V;
    if (Kind != k_Imm || !HasHash)
      return false;
    // Symbolic values are resolved by a fixup.
    return !getConstant(V) || getImm32(V);
  }
  bool isRtsdImm() const {
    int64_t V;
    return isHashImmInRange(0, 1020) && getConstant(V) && V % 4 == 0;
  }
  bool isMvfacImm() const { return isHashImmInRange(0, 2); }
  bool isRacImm() const { return isHashImmInRange(1, 2); }
  bool isBfLsb() const { return isHashImmInRange(0, 31); }
  bool isBfWidth() const { return isHashImmInRange(1, 31); }

  bool isDsp5B() const { return isDsp(31, 1); }
  bool isDsp5W() const { return isDsp(31, 2); }
  bool isDsp5L() const { return isDsp(31, 4); }
  bool isDsp8B() const { return isDsp(255, 1); }
  bool isDsp8W() const { return isDsp(255, 2); }
  bool isDsp8L() const { return isDsp(255, 4); }
  bool isDsp16B() const { return isDsp(65535, 1); }
  bool isDsp16W() const { return isDsp(65535, 2); }
  bool isDsp16L() const { return isDsp(65535, 4); }
  bool isDsp8D() const { return isDsp(255, 4); }
  bool isDsp16D() const { return isDsp(65535, 4); }

  bool isBrTarget() const { return Kind == k_Imm && !HasHash; }

  void addRegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "invalid number of operands");
    Inst.addOperand(MCOperand::createReg(getReg()));
  }

  void addImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "invalid number of operands");
    int64_t V;
    if (getConstant(V))
      Inst.addOperand(MCOperand::createImm(V));
    else
      Inst.addOperand(MCOperand::createExpr(Imm));
  }

  void print(raw_ostream &OS, const MCAsmInfo &MAI) const override {
    switch (Kind) {
    case k_Tok:
      OS << "Token " << Tok;
      break;
    case k_Reg:
      OS << "Register " << Reg.id();
      break;
    case k_Imm:
      OS << (HasHash ? "Immediate #" : "Expression ");
      MAI.printExpr(OS, *Imm);
      break;
    }
  }
};

} // end anonymous namespace

#define GET_REGISTER_MATCHER
#define GET_MATCHER_IMPLEMENTATION
#define GET_MNEMONIC_SPELL_CHECKER
#include "RXGenAsmMatcher.inc"

// "0[Rn]" that cannot use the dsp5 form (a register above R7) is the
// displacement-less "[Rn]" form, as in GNU as. Returns the opcode without the
// trailing displacement operand, or 0.
static unsigned getZeroDspOpcode(unsigned Opc) {
  switch (Opc) {
  case RX::MOV_B_ld8: return RX::MOV_B_ld0;
  case RX::MOV_W_ld8: return RX::MOV_W_ld0;
  case RX::MOV_L_ld8: return RX::MOV_L_ld0;
  case RX::MOV_B_st8: return RX::MOV_B_st0;
  case RX::MOV_W_st8: return RX::MOV_W_st0;
  case RX::MOV_L_st8: return RX::MOV_L_st0;
  case RX::MOVU_B_ld8: return RX::MOVU_B_ld0;
  case RX::MOVU_W_ld8: return RX::MOVU_W_ld0;
  case RX::FSUB_m8: return RX::FSUB_m0;
  case RX::FCMP_m8: return RX::FCMP_m0;
  case RX::FADD_m8: return RX::FADD_m0;
  case RX::FMUL_m8: return RX::FMUL_m0;
  case RX::FDIV_m8: return RX::FDIV_m0;
  case RX::FTOI_m8: return RX::FTOI_m0;
  case RX::ROUND_m8: return RX::ROUND_m0;
  case RX::FSQRT_m8: return RX::FSQRT_m0;
  case RX::FTOU_m8: return RX::FTOU_m0;
  case RX::DMOV_D_st8: return RX::DMOV_D_st0;
  case RX::DMOV_D_ld8: return RX::DMOV_D_ld0;
  default: return 0;
  }
}

// mov.sz #imm, dsp:8[Rd] / dsp:16[Rd], the form without displacement and
// the dsp:5 form, by size and immediate field.
struct MovImmMem {
  unsigned D8, D16, D0, D5, Scale;
};
static const MovImmMem MovImmMems[] = {
    {RX::MOV_B_mi8_b8, RX::MOV_B_mi16_b8, RX::MOV_B_mi0_b8, RX::MOV_B_mi5, 1},
    {RX::MOV_W_mi8_w8, RX::MOV_W_mi16_w8, RX::MOV_W_mi0_w8, RX::MOV_W_mi5, 2},
    {RX::MOV_W_mi8_w16, RX::MOV_W_mi16_w16, RX::MOV_W_mi0_w16, RX::MOV_W_mi5,
     2},
    {RX::MOV_L_mi8_s8, RX::MOV_L_mi16_s8, RX::MOV_L_mi0_s8, RX::MOV_L_mi5, 4},
    {RX::MOV_L_mi8_s16, RX::MOV_L_mi16_s16, RX::MOV_L_mi0_s16, RX::MOV_L_mi5,
     4},
    {RX::MOV_L_mi8_s24, RX::MOV_L_mi16_s24, RX::MOV_L_mi0_s24, RX::MOV_L_mi5,
     4},
    {RX::MOV_L_mi8_i32, RX::MOV_L_mi16_i32, RX::MOV_L_mi0_i32, RX::MOV_L_mi5,
     4},
};

// mov.sz #imm, dsp[Rd] (operands rd, dsp, imm): like GNU as, use the dsp:5
// form when Rd is R0-R7, the displacement fits and #imm is 0-255, and the
// form without displacement for 0.
static void shortenMovImmMem(MCInst &Inst, const MCRegisterInfo *MRI) {
  for (const MovImmMem &F : MovImmMems) {
    bool IsD8 = Inst.getOpcode() == F.D8;
    if (!IsD8 && Inst.getOpcode() != F.D16)
      continue;
    const MCOperand &Dsp = Inst.getOperand(1);
    const MCOperand &Imm = Inst.getOperand(2);
    if (!Dsp.isImm())
      return;
    bool SmallImm = Imm.isImm() && Imm.getImm() >= 0 && Imm.getImm() <= 255;
    unsigned Opc = 0;
    if (IsD8 && SmallImm &&
        MRI->getEncodingValue(Inst.getOperand(0).getReg()) < 8 &&
        Dsp.getImm() / F.Scale <= 31)
      Opc = F.D5;
    else if (Dsp.getImm() == 0)
      Opc = F.D0;
    if (!Opc)
      return;
    MCInst New;
    New.setOpcode(Opc);
    New.addOperand(Inst.getOperand(0));
    if (Opc == F.D5)
      New.addOperand(Dsp);
    New.addOperand(Imm);
    Inst = New;
    return;
  }
}

// mov.sz src, dest between memory, by size, source and destination
// displacement (none, dsp:8, dsp:16).
static const unsigned MovMemMem[3][3][3] = {
    {{RX::MOV_B_mm0_0, RX::MOV_B_mm0_8, RX::MOV_B_mm0_16},
     {RX::MOV_B_mm8_0, RX::MOV_B_mm8_8, RX::MOV_B_mm8_16},
     {RX::MOV_B_mm16_0, RX::MOV_B_mm16_8, RX::MOV_B_mm16_16}},
    {{RX::MOV_W_mm0_0, RX::MOV_W_mm0_8, RX::MOV_W_mm0_16},
     {RX::MOV_W_mm8_0, RX::MOV_W_mm8_8, RX::MOV_W_mm8_16},
     {RX::MOV_W_mm16_0, RX::MOV_W_mm16_8, RX::MOV_W_mm16_16}},
    {{RX::MOV_L_mm0_0, RX::MOV_L_mm0_8, RX::MOV_L_mm0_16},
     {RX::MOV_L_mm8_0, RX::MOV_L_mm8_8, RX::MOV_L_mm8_16},
     {RX::MOV_L_mm16_0, RX::MOV_L_mm16_8, RX::MOV_L_mm16_16}},
};

// Operands rs, [sdsp], rd, [ddsp]: drop a zero displacement on either side,
// as GNU as does.
static void shortenMovMemMem(MCInst &Inst) {
  for (unsigned Sz = 0; Sz != 3; ++Sz) {
    for (unsigned S = 0; S != 3; ++S) {
      for (unsigned D = 0; D != 3; ++D) {
        if (Inst.getOpcode() != MovMemMem[Sz][S][D])
          continue;
        unsigned I = 0;
        MCOperand Rs = Inst.getOperand(I++);
        MCOperand SDsp = S ? Inst.getOperand(I++) : MCOperand();
        MCOperand Rd = Inst.getOperand(I++);
        MCOperand DDsp = D ? Inst.getOperand(I++) : MCOperand();
        unsigned NS = S && SDsp.isImm() && SDsp.getImm() == 0 ? 0 : S;
        unsigned ND = D && DDsp.isImm() && DDsp.getImm() == 0 ? 0 : D;
        if (NS == S && ND == D)
          return;
        MCInst New;
        New.setOpcode(MovMemMem[Sz][NS][ND]);
        New.addOperand(Rs);
        if (NS)
          New.addOperand(SDsp);
        New.addOperand(Rd);
        if (ND)
          New.addOperand(DDsp);
        Inst = New;
        return;
      }
    }
  }
}

bool RXAsmParser::matchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                                          OperandVector &Operands,
                                          MCStreamer &Out, uint64_t &ErrorInfo,
                                          bool MatchingInlineAsm) {
  if (SizelessBranchKind) {
    MCInst Inst;
    Inst.setOpcode(RX::BR_SIZELESS);
    Inst.addOperand(MCOperand::createExpr(
        static_cast<RXOperand &>(*Operands[1]).getImm()));
    Inst.addOperand(MCOperand::createImm(*SizelessBranchKind));
    Inst.addOperand(MCOperand::createImm(0));
    Inst.setLoc(IDLoc);
    SizelessBranchKind.reset();
    Out.emitInstruction(Inst, getSTI());
    return false;
  }

  MCInst Inst;
  FeatureBitset MissingFeatures;
  switch (MatchInstructionImpl(Operands, Inst, ErrorInfo, MissingFeatures,
                               MatchingInlineAsm)) {
  case Match_Success:
    switch (Inst.getOpcode()) {
    case RX::DPUSHM_D:
    case RX::DPUSHM_L:
    case RX::DPOPM_D:
    case RX::DPOPM_L: {
      const MCRegisterInfo *MRI = getContext().getRegisterInfo();
      if (MRI->getEncodingValue(Inst.getOperand(1).getReg()) <
          MRI->getEncodingValue(Inst.getOperand(0).getReg()))
        return Error(IDLoc, "register range must be ascending");
      break;
    }
    default:
      break;
    }
    shortenMovImmMem(Inst, getContext().getRegisterInfo());
    shortenMovMemMem(Inst);
    if (unsigned Opc = getZeroDspOpcode(Inst.getOpcode())) {
      const MCOperand &Dsp = Inst.getOperand(Inst.getNumOperands() - 1);
      if (Dsp.isImm() && Dsp.getImm() == 0) {
        MCInst Short;
        Short.setOpcode(Opc);
        for (unsigned I = 0, E = Inst.getNumOperands() - 1; I != E; ++I)
          Short.addOperand(Inst.getOperand(I));
        Inst = Short;
      }
    }
    Inst.setLoc(IDLoc);
    Out.emitInstruction(Inst, getSTI());
    noteRXInstruction(Out, Inst.getOpcode());
    return false;
  case Match_MissingFeature:
    return Error(IDLoc, "instruction requires a CPU feature not currently "
                        "enabled (e.g. -mcpu=rxv3)");
  case Match_MnemonicFail: {
    FeatureBitset FBS = ComputeAvailableFeatures(getSTI().getFeatureBits());
    std::string Suggestion = RXMnemonicSpellCheck(
        ((RXOperand &)*Operands[0]).getToken(), FBS, 0);
    return Error(IDLoc, "invalid instruction" + Suggestion);
  }
  case Match_InvalidOperand: {
    SMLoc ErrorLoc = IDLoc;
    if (ErrorInfo != ~0ULL) {
      if (ErrorInfo >= Operands.size())
        return Error(IDLoc, "too few operands for instruction");
      ErrorLoc = ((RXOperand &)*Operands[ErrorInfo]).getStartLoc();
      if (ErrorLoc == SMLoc())
        ErrorLoc = IDLoc;
    }
    return Error(ErrorLoc, "invalid operand for instruction");
  }
  default:
    return Error(IDLoc, "invalid instruction");
  }
}

bool RXAsmParser::parseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                SMLoc &EndLoc) {
  if (!tryParseRegister(Reg, StartLoc, EndLoc).isSuccess())
    return Error(StartLoc, "invalid register name");
  return false;
}

ParseStatus RXAsmParser::tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                          SMLoc &EndLoc) {
  const AsmToken &Tok = getParser().getTok();
  StartLoc = Tok.getLoc();
  EndLoc = Tok.getEndLoc();
  if (Tok.isNot(AsmToken::Identifier))
    return ParseStatus::NoMatch;
  std::string Name = Tok.getIdentifier().lower();
  // SP is R0; DCR0-DCR3 are the DFPU control registers.
  static const MCPhysReg DCRs[] = {RX::DPSW, RX::DCMR, RX::DECNT, RX::DEPC};
  if (Name == "sp")
    Reg = RX::R0;
  else if (Name.size() == 4 && StringRef(Name).starts_with("dcr") &&
           Name[3] >= '0' && Name[3] <= '3')
    Reg = DCRs[Name[3] - '0'];
  else
    Reg = MatchRegisterName(Name);
  // General-purpose, control, accumulator and DFPU registers are written by
  // name in operands.
  static const unsigned Classes[] = {
      RX::GPRRegClassID, RX::CTRLRegClassID, RX::TRCTRLRegClassID,
      RX::ACCRegClassID, RX::DPRRegClassID,  RX::DRHRegClassID,
      RX::DRLRegClassID, RX::DCRRegClassID};
  const MCRegisterInfo *MRI = getContext().getRegisterInfo();
  if (!Reg || llvm::none_of(Classes, [&](unsigned RC) {
        return MRI->getRegClass(RC).contains(Reg);
      }))
    return ParseStatus::NoMatch;
  // The Protect Zone control registers exist only on RXv3e; elsewhere the
  // names are ordinary symbols.
  switch (Reg.id()) {
  case RX::IVC:
  case RX::WPRI:
  case RX::WID:
  case RX::TRCTB:
  case RX::WINTB:
    if (!getSTI().hasFeature(RX::FeatureRXv3e))
      return ParseStatus::NoMatch;
    break;
  default:
    break;
  }
  getParser().Lex();
  return ParseStatus::Success;
}

static bool takesLongMemoryOperand(StringRef Mnemonic) {
  static const char *const Names[] = {"fsub", "fcmp",  "fadd",  "fmul", "fdiv",
                                      "ftoi", "round", "fsqrt", "ftou"};
  for (const char *N : Names)
    if (Mnemonic.equals_insensitive(N))
      return true;
  return false;
}

// "[" Rn "]", "[" Rn "+" "]", "[" "-" Rn "]" or "[" Ri "," Rb "]"
bool RXAsmParser::parseBracketedRegister(OperandVector &Operands) {
  SMLoc S = getLexer().getLoc();
  Operands.push_back(RXOperand::createToken("[", S));
  getParser().Lex(); // eat '['
  if (getLexer().is(AsmToken::Minus)) {
    Operands.push_back(RXOperand::createToken("-", getLexer().getLoc()));
    getParser().Lex();
  }
  MCRegister Reg;
  SMLoc RS, RE;
  if (parseRegister(Reg, RS, RE))
    return true;
  Operands.push_back(RXOperand::createReg(Reg, RS, RE));
  if (getLexer().is(AsmToken::Plus)) {
    Operands.push_back(RXOperand::createToken("+", getLexer().getLoc()));
    getParser().Lex();
  } else if (parseOptionalToken(AsmToken::Comma)) {
    if (parseRegister(Reg, RS, RE))
      return true;
    Operands.push_back(RXOperand::createReg(Reg, RS, RE));
  }
  SMLoc E = getLexer().getLoc();
  if (parseToken(AsmToken::RBrac, "expected ']'"))
    return true;
  Operands.push_back(RXOperand::createToken("]", E));
  // The FPU instructions only take 32-bit memory operands; GNU as accepts an
  // optional ".L" after the bracket.
  if (getLexer().is(AsmToken::Identifier) &&
      getLexer().getTok().getIdentifier().equals_insensitive(".l") &&
      takesLongMemoryOperand(
          static_cast<RXOperand &>(*Operands[0]).getToken()))
    getParser().Lex();
  return false;
}

bool RXAsmParser::parseOperand(OperandVector &Operands) {
  SMLoc S = getLexer().getLoc();

  // setpsw / clrpsw take a flag name (c z s o i u), matched as a token.
  StringRef Mnemonic = static_cast<RXOperand &>(*Operands[0]).getToken();
  if ((Mnemonic.equals_insensitive("setpsw") ||
       Mnemonic.equals_insensitive("clrpsw")) &&
      getLexer().is(AsmToken::Identifier)) {
    StringRef Flag = getLexer().getTok().getIdentifier();
    static const char *const Flags[] = {"c", "z", "s", "o", "i", "u"};
    for (const char *F : Flags) {
      if (Flag.equals_insensitive(F)) {
        Operands.push_back(RXOperand::createToken(F, S));
        getParser().Lex();
        return false;
      }
    }
  }

  // #imm
  if (getLexer().is(AsmToken::Hash)) {
    getParser().Lex();
    const MCExpr *E;
    if (getParser().parseExpression(E))
      return true;
    Operands.push_back(
        RXOperand::createImm(E, /*HasHash=*/true, S, getLexer().getLoc()));
    return false;
  }

  // [Rn]
  if (getLexer().is(AsmToken::LBrac))
    return parseBracketedRegister(Operands);

  // Rn or Rlo-Rhi
  MCRegister Reg;
  SMLoc RS, RE;
  if (tryParseRegister(Reg, RS, RE).isSuccess()) {
    Operands.push_back(RXOperand::createReg(Reg, RS, RE));
    if (getLexer().is(AsmToken::Minus)) {
      Operands.push_back(RXOperand::createToken("-", getLexer().getLoc()));
      getParser().Lex();
      if (parseRegister(Reg, RS, RE))
        return true;
      Operands.push_back(RXOperand::createReg(Reg, RS, RE));
    }
    return false;
  }

  // dsp[Rn], or a bare expression (branch target).
  const MCExpr *E;
  if (getParser().parseExpression(E))
    return true;
  Operands.push_back(
      RXOperand::createImm(E, /*HasHash=*/false, S, getLexer().getLoc()));
  if (getLexer().is(AsmToken::LBrac))
    return parseBracketedRegister(Operands);
  return false;
}

// RX has no SUB #imm with an immediate wider than 4 bits; GNU as emits the
// 3-operand ADD #-imm, Rd, Rd instead (never the uimm4 ADD form).
void RXAsmParser::rewriteSubImmediate(OperandVector &Operands) {
  if (Operands.size() != 3)
    return;
  auto &Mnemonic = static_cast<RXOperand &>(*Operands[0]);
  auto &Src = static_cast<RXOperand &>(*Operands[1]);
  auto &Dst = static_cast<RXOperand &>(*Operands[2]);
  int64_t V;
  if (!Mnemonic.getToken().equals_insensitive("sub") || !Src.isHashImm() ||
      !Dst.isReg() || !Src.getImm()->evaluateAsAbsolute(V) ||
      (V >= 0 && V <= 15))
    return;
  SMLoc DS = Dst.getStartLoc(), DE = Dst.getEndLoc();
  MCRegister Reg = Dst.getReg();
  Operands[0] = RXOperand::createToken("add", Mnemonic.getStartLoc());
  Operands[1] = RXOperand::createImm(MCConstantExpr::create(-V, getContext()),
                                     /*HasHash=*/true, Src.getStartLoc(),
                                     Src.getEndLoc());
  Operands.push_back(RXOperand::createReg(Reg, DS, DE));
}

// bra / bsr / b<cond> without a size: "bra Rn" and "bsr Rn" are the .L
// forms; to a label, the kind of the BR_SIZELESS that matchAndEmitInstruction
// emits.
bool RXAsmParser::parseSizelessBranch(StringRef Name, SMLoc NameLoc,
                                      OperandVector &Operands) {
  // Condition names, including GNU as's alternatives, by BCnd code.
  static const struct {
    const char *Name;
    unsigned Cond;
  } Conds[] = {{"eq", 0},  {"z", 0},   {"ne", 1},  {"nz", 1}, {"geu", 2},
               {"c", 2},   {"ltu", 3}, {"nc", 3},  {"gtu", 4}, {"leu", 5},
               {"pz", 6},  {"n", 7},   {"ge", 8},  {"lt", 9},  {"gt", 10},
               {"le", 11}, {"o", 12},  {"no", 13}};
  std::optional<unsigned> Kind;
  if (Name.equals_insensitive("bra"))
    Kind = RX::BrKindBra;
  else if (Name.equals_insensitive("bsr"))
    Kind = RX::BrKindBsr;
  else if (Name.size() > 1 && (Name[0] == 'b' || Name[0] == 'B'))
    for (const auto &C : Conds)
      if (Name.drop_front().equals_insensitive(C.Name))
        Kind = RX::BrKindCond + C.Cond;
  if (!Kind || Operands.size() != 2)
    return false;
  auto &Op = static_cast<RXOperand &>(*Operands[1]);
  if (Op.isReg() && *Kind <= RX::BrKindBsr) {
    Operands[0] = RXOperand::createToken(
        *Kind == RX::BrKindBra ? "bra.l" : "bsr.l", NameLoc);
    return false;
  }
  if (!Op.isBrTarget())
    return false;
  int64_t V;
  if (Op.getImm()->evaluateAsAbsolute(V))
    return Error(Op.getStartLoc(), "a branch without a size needs a label; "
                                   "give the size for a displacement");
  SizelessBranchKind = *Kind;
  return false;
}

bool RXAsmParser::parseInstruction(ParseInstructionInfo &Info, StringRef Name,
                                   SMLoc NameLoc, OperandVector &Operands) {
  SizelessBranchKind.reset();
  // GNU as's sizes for these mnemonics when none is given.
  static const struct {
    const char *Name, *Sized;
  } DefaultSizes[] = {{"mov", "mov.l"},       {"movu", "movu.w"},
                      {"push", "push.l"},     {"sstr", "sstr.l"},
                      {"suntil", "suntil.l"}, {"swhile", "swhile.l"},
                      {"rmpa", "rmpa.l"}};
  bool DefaultMov = false;
  for (const auto &D : DefaultSizes) {
    if (Name.equals_insensitive(D.Name)) {
      DefaultMov = Name.equals_insensitive("mov");
      Name = D.Sized;
      break;
    }
  }

  Operands.push_back(RXOperand::createToken(Name, NameLoc));

  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    if (parseOperand(Operands))
      return true;
    while (parseOptionalToken(AsmToken::Comma))
      if (parseOperand(Operands))
        return true;
  }

  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("unexpected token in operand list");
  getParser().Lex(); // eat EndOfStatement

  // GNU as wants the size of an immediate stored to memory.
  if (DefaultMov && Operands.size() > 2 &&
      static_cast<RXOperand &>(*Operands[1]).isHashImm() &&
      llvm::any_of(Operands, [](const auto &Op) {
        auto &RXOp = static_cast<RXOperand &>(*Op);
        return RXOp.isToken() && RXOp.getToken() == "[";
      }))
    return Error(NameLoc, "mov #imm to memory needs a size (.b, .w or .l)");

  if (parseSizelessBranch(Name, NameLoc, Operands))
    return true;
  rewriteSubImmediate(Operands);
  return false;
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeRXAsmParser() {
  RegisterMCAsmParser<RXAsmParser> X(getTheRXTarget());
}
