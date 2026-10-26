//===- RXISelLowering.cpp - RX DAG Lowering Implementation ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the RXTargetLowering class.
//
//===----------------------------------------------------------------------===//

#include "RXISelLowering.h"
#include "RX.h"
#include "RXMachineFunctionInfo.h"
#include "RXSelectionDAGInfo.h"
#include "RXSubtarget.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define DEBUG_TYPE "rx-lower"

RXTargetLowering::RXTargetLowering(const TargetMachine &TM,
                                   const RXSubtarget &STI)
    : TargetLowering(TM, STI), STI(STI) {
  addRegisterClass(MVT::i32, &RX::GPRRegClass);
  // float with +fpu: see below.
  if (STI.hasFPU())
    addRegisterClass(MVT::f32, &RX::GPRRegClass);
  computeRegisterProperties(STI.getRegisterInfo());

  setStackPointerRegisterToSaveRestore(RX::R0);
  setBooleanContents(ZeroOrOneBooleanContent);
  setSchedulingPreference(Sched::RegPressure);

  for (MVT VT : MVT::integer_valuetypes()) {
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::i1, Promote);
  }

  setOperationAction(ISD::GlobalAddress, MVT::i32, Custom);
  setOperationAction(ISD::ExternalSymbol, MVT::i32, Custom);
  setOperationAction(ISD::BlockAddress, MVT::i32, Custom);

  // Comparisons go through RXISD::CMP and the flags in PSW.
  setOperationAction(ISD::BR_CC, MVT::i32, Custom);
  setOperationAction(ISD::BRCOND, MVT::Other, Expand);
  setOperationAction(ISD::SETCC, MVT::i32, Custom);
  setOperationAction(ISD::SELECT, MVT::i32, Expand);
  setOperationAction(ISD::SELECT_CC, MVT::i32, Custom);
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);
  setOperationAction(ISD::JumpTable, MVT::i32, Expand);
  setMinimumJumpTableEntries(UINT_MAX);

  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);

  // Operations without an RX instruction.
  for (unsigned Op :
       {ISD::SREM, ISD::UREM, ISD::SDIVREM, ISD::UDIVREM, ISD::MULHS,
        ISD::MULHU, ISD::SMUL_LOHI, ISD::UMUL_LOHI, ISD::CTLZ, ISD::CTTZ,
        ISD::CTPOP, ISD::UMIN, ISD::UMAX, ISD::SHL_PARTS, ISD::SRL_PARTS,
        ISD::SRA_PARTS, ISD::UADDO_CARRY, ISD::USUBO_CARRY, ISD::BITREVERSE})
    setOperationAction(Op, MVT::i32, Expand);

  // Wider additions and subtractions use the carry chain (add/adc,
  // sub/sbb).
  for (unsigned Op : {ISD::ADDC, ISD::ADDE, ISD::SUBC, ISD::SUBE})
    setOperationAction(Op, MVT::i32, Legal);

  // double has no register class: it is softened to integers and its
  // operations become libgcc calls (__adddf3, ...), which matches the GCC
  // -m64bit-doubles ABI (values in general registers). So is float without
  // +fpu; with it, float lives in the general registers (where the FPU works
  // on it, and where the ABI passes it anyway) and only fast-math operations
  // in functions that allow flushing denormals use FPU instructions; see
  // canUseFPU, lowerFPArith and emitCMP.
  if (STI.hasFPU()) {
    for (unsigned Op : {ISD::FADD, ISD::FSUB, ISD::FMUL, ISD::FDIV})
      setOperationAction(Op, MVT::f32, Custom);
    setOperationAction(ISD::FSQRT, MVT::f32,
                       STI.hasRXv2() ? Custom : LibCall);
    for (unsigned Op : {ISD::SETCC, ISD::BR_CC, ISD::SELECT_CC})
      setOperationAction(Op, MVT::f32, Custom);
    setOperationAction(ISD::SELECT, MVT::f32, Expand);
    setOperationAction(ISD::ConstantFP, MVT::f32, Legal);

    // itof / utof are exact for any input; ftoi would trap on a denormal
    // (FPSW.DN = 0), so float to integer stays a libcall.
    setOperationAction(ISD::UINT_TO_FP, MVT::i32,
                       STI.hasRXv2() ? Legal : LibCall);
    setOperationAction(ISD::FP_TO_SINT, MVT::i32, LibCall);
    setOperationAction(ISD::FP_TO_UINT, MVT::i32, LibCall);

    // Everything else is bit manipulation or a libcall.
    for (unsigned Op :
         {ISD::FNEG, ISD::FABS, ISD::FCOPYSIGN, ISD::FREM, ISD::FMA,
          ISD::FMAD, ISD::FMINNUM, ISD::FMAXNUM, ISD::FMINIMUM,
          ISD::FMAXIMUM, ISD::FMINIMUMNUM, ISD::FMAXIMUMNUM, ISD::FCEIL,
          ISD::FFLOOR, ISD::FTRUNC, ISD::FRINT, ISD::FNEARBYINT,
          ISD::FROUND, ISD::FROUNDEVEN, ISD::FPOW, ISD::FPOWI, ISD::FSIN,
          ISD::FCOS, ISD::FTAN, ISD::FSINCOS, ISD::FEXP, ISD::FEXP2,
          ISD::FEXP10, ISD::FLOG, ISD::FLOG2, ISD::FLOG10, ISD::FLDEXP,
          ISD::FFREXP, ISD::FCANONICALIZE, ISD::IS_FPCLASS, ISD::LROUND,
          ISD::LLROUND, ISD::LRINT, ISD::LLRINT, ISD::FP_TO_FP16,
          ISD::FP_TO_BF16, ISD::FP16_TO_FP, ISD::BF16_TO_FP})
      setOperationAction(Op, MVT::f32, Expand);
  }

  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i32, Expand);
  setOperationAction(ISD::STACKSAVE, MVT::Other, Expand);
  setOperationAction(ISD::STACKRESTORE, MVT::Other, Expand);
  setOperationAction(ISD::VASTART, MVT::Other, Expand);
  setOperationAction(ISD::VAARG, MVT::Other, Expand);
  setOperationAction(ISD::VAEND, MVT::Other, Expand);
  setOperationAction(ISD::VACOPY, MVT::Other, Expand);

  // Atomic loads and stores of up to 32 bits are plain MOVs and fences are
  // compiler barriers (RX parts are single-core). A 32-bit compare-and-swap
  // is the CMPXCHG32 pseudo (see RXAsmPrinter); AtomicExpand turns narrower
  // ones into it, and read-modify-write operations into a loop around it.
  setMaxAtomicSizeInBitsSupported(32);
  setMinCmpXchgSizeInBits(32);
  setOperationAction(ISD::ATOMIC_FENCE, MVT::Other, Custom);

  // llvm.trap / llvm.debugtrap are BRK (the default would call abort()).
  setOperationAction(ISD::TRAP, MVT::Other, Legal);
  setOperationAction(ISD::DEBUGTRAP, MVT::Other, Legal);

  setMinFunctionAlignment(Align(1));
  setPrefFunctionAlignment(Align(1));
}

SDValue RXTargetLowering::LowerOperation(SDValue Op, SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  case ISD::GlobalAddress:
    return lowerGlobalAddress(Op, DAG);
  case ISD::ExternalSymbol:
    return lowerExternalSymbol(Op, DAG);
  case ISD::BlockAddress:
    return lowerBlockAddress(Op, DAG);
  case ISD::BR_CC:
    return lowerBR_CC(Op, DAG);
  case ISD::SETCC:
    return lowerSETCC(Op, DAG);
  case ISD::SELECT_CC:
    return lowerSELECT_CC(Op, DAG);
  case ISD::FADD:
  case ISD::FSUB:
  case ISD::FMUL:
  case ISD::FDIV:
  case ISD::FSQRT:
    return lowerFPArith(Op, DAG);
  case ISD::ATOMIC_FENCE:
    // Single-core: only the compiler must not reorder across a fence.
    return DAG.getNode(ISD::MEMBARRIER, SDLoc(Op), MVT::Other,
                       Op.getOperand(0));
  default:
    llvm_unreachable("unexpected operation to lower");
  }
}

SDValue RXTargetLowering::lowerGlobalAddress(SDValue Op,
                                             SelectionDAG &DAG) const {
  auto *N = cast<GlobalAddressSDNode>(Op);
  SDLoc DL(Op);
  EVT PtrVT = Op.getValueType();
  SDValue Addr =
      DAG.getTargetGlobalAddress(N->getGlobal(), DL, PtrVT, N->getOffset());
  return DAG.getNode(RXISD::Wrapper, DL, PtrVT, Addr);
}

SDValue RXTargetLowering::lowerExternalSymbol(SDValue Op,
                                              SelectionDAG &DAG) const {
  auto *N = cast<ExternalSymbolSDNode>(Op);
  EVT PtrVT = Op.getValueType();
  SDValue Sym = DAG.getTargetExternalSymbol(N->getSymbol(), PtrVT);
  return DAG.getNode(RXISD::Wrapper, SDLoc(Op), PtrVT, Sym);
}

SDValue RXTargetLowering::lowerBlockAddress(SDValue Op,
                                            SelectionDAG &DAG) const {
  auto *N = cast<BlockAddressSDNode>(Op);
  EVT PtrVT = Op.getValueType();
  SDValue BA = DAG.getTargetBlockAddress(N->getBlockAddress(), PtrVT);
  return DAG.getNode(RXISD::Wrapper, SDLoc(Op), PtrVT, BA);
}

// The RX FPU gives IEEE results only for operands that are not NaN,
// infinite or denormal: denormal operands and results raise an
// unimplemented-processing exception with FPSW.DN = 0 and are flushed to a
// zero of the same sign with DN = 1 (the reset value, which rx-rt also
// sets). The code generator therefore uses it only for operations that may
// be assumed not to involve NaNs or infinities (nnan and ninf) in functions
// whose denormal environment for the type allows flushing with the sign kept
// (denormal_fpenv(preservesign), e.g. -fdenormal-fp-math=preserve-sign);
// with the default IEEE environment everything stays soft-float.
static bool canUseFPU(SDNodeFlags Flags, const SelectionDAG &DAG, EVT VT) {
  if (!Flags.hasNoNaNs() || !Flags.hasNoInfs())
    return false;
  return DAG.getMachineFunction().getDenormalMode(VT.getFltSemantics()) ==
         DenormalMode::getPreserveSign();
}

// float arithmetic: an FPU instruction for fast-math operations (the node is
// legal as is), a libcall otherwise.
SDValue RXTargetLowering::lowerFPArith(SDValue Op, SelectionDAG &DAG) const {
  if (canUseFPU(Op->getFlags(), DAG, Op.getValueType()))
    return Op;
  RTLIB::Libcall LC;
  switch (Op.getOpcode()) {
  case ISD::FADD: LC = RTLIB::ADD_F32; break;
  case ISD::FSUB: LC = RTLIB::SUB_F32; break;
  case ISD::FMUL: LC = RTLIB::MUL_F32; break;
  case ISD::FDIV: LC = RTLIB::DIV_F32; break;
  case ISD::FSQRT: LC = RTLIB::SQRT_F32; break;
  default:
    llvm_unreachable("unexpected float operation");
  }
  SmallVector<SDValue, 2> Ops(Op->ops());
  return makeLibCall(DAG, LC, MVT::f32, Ops, MakeLibCallOptions(), SDLoc(Op))
      .first;
}

// Emits "cmp RHS, LHS" (flags for LHS - RHS) and returns the glue; sets CC
// to the RX condition that is true when "LHS cond RHS" holds. A float
// compare is an FCMP for fast-math code (no NaNs, so the O flag is clear and
// the signed conditions apply), and a libcall followed by an integer compare
// otherwise.
SDValue RXTargetLowering::emitCMP(SDValue LHS, SDValue RHS,
                                  ISD::CondCode Cond, SDNodeFlags Flags,
                                  RXCC::CondCode &CC, const SDLoc &DL,
                                  SelectionDAG &DAG) const {
  if (LHS.getValueType() == MVT::f32) {
    if (canUseFPU(Flags, DAG, MVT::f32) && Cond != ISD::SETO &&
        Cond != ISD::SETUO) {
      switch (Cond) {
      case ISD::SETEQ: case ISD::SETOEQ: case ISD::SETUEQ:
        CC = RXCC::EQ; break;
      case ISD::SETNE: case ISD::SETONE: case ISD::SETUNE:
        CC = RXCC::NE; break;
      case ISD::SETLT: case ISD::SETOLT: case ISD::SETULT:
        CC = RXCC::LT; break;
      case ISD::SETLE: case ISD::SETOLE: case ISD::SETULE:
        CC = RXCC::LE; break;
      case ISD::SETGT: case ISD::SETOGT: case ISD::SETUGT:
        CC = RXCC::GT; break;
      case ISD::SETGE: case ISD::SETOGE: case ISD::SETUGE:
        CC = RXCC::GE; break;
      default:
        llvm_unreachable("unexpected float condition");
      }
      return DAG.getNode(RXISD::FCMP, DL, MVT::Glue, LHS, RHS);
    }
    SDValue NewLHS = LHS, NewRHS = RHS;
    softenSetCCOperands(DAG, MVT::f32, NewLHS, NewRHS, Cond, DL, LHS, RHS);
    if (!NewRHS) {
      // NewLHS is already the boolean result.
      NewRHS = DAG.getConstant(0, DL, NewLHS.getValueType());
      Cond = ISD::SETNE;
    }
    LHS = NewLHS;
    RHS = NewRHS;
  }

  // Keep a constant on the right so it folds into the compare.
  if (isa<ConstantSDNode>(LHS) && !isa<ConstantSDNode>(RHS)) {
    std::swap(LHS, RHS);
    Cond = ISD::getSetCCSwappedOperands(Cond);
  }

  switch (Cond) {
  case ISD::SETEQ:  CC = RXCC::EQ; break;
  case ISD::SETNE:  CC = RXCC::NE; break;
  case ISD::SETLT:  CC = RXCC::LT; break;
  case ISD::SETLE:  CC = RXCC::LE; break;
  case ISD::SETGT:  CC = RXCC::GT; break;
  case ISD::SETGE:  CC = RXCC::GE; break;
  case ISD::SETULT: CC = RXCC::LTU; break;
  case ISD::SETULE: CC = RXCC::LEU; break;
  case ISD::SETUGT: CC = RXCC::GTU; break;
  case ISD::SETUGE: CC = RXCC::GEU; break;
  default:
    llvm_unreachable("unexpected integer condition");
  }
  return DAG.getNode(RXISD::CMP, DL, MVT::Glue, LHS, RHS);
}

SDValue RXTargetLowering::lowerBR_CC(SDValue Op, SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  ISD::CondCode Cond = cast<CondCodeSDNode>(Op.getOperand(1))->get();
  SDLoc DL(Op);
  RXCC::CondCode CC;
  SDValue Glue = emitCMP(Op.getOperand(2), Op.getOperand(3), Cond,
                         Op->getFlags(), CC, DL, DAG);
  return DAG.getNode(RXISD::BR_CC, DL, MVT::Other, Chain, Op.getOperand(4),
                     DAG.getConstant(CC, DL, MVT::i32), Glue);
}

SDValue RXTargetLowering::lowerSETCC(SDValue Op, SelectionDAG &DAG) const {
  ISD::CondCode Cond = cast<CondCodeSDNode>(Op.getOperand(2))->get();
  SDLoc DL(Op);
  RXCC::CondCode CC;
  SDValue Glue = emitCMP(Op.getOperand(0), Op.getOperand(1), Cond,
                         Op->getFlags(), CC, DL, DAG);
  return DAG.getNode(RXISD::SETCC, DL, MVT::i32,
                     DAG.getConstant(CC, DL, MVT::i32), Glue);
}

SDValue RXTargetLowering::lowerSELECT_CC(SDValue Op, SelectionDAG &DAG) const {
  ISD::CondCode Cond = cast<CondCodeSDNode>(Op.getOperand(4))->get();
  SDLoc DL(Op);
  RXCC::CondCode CC;
  SDValue Glue = emitCMP(Op.getOperand(0), Op.getOperand(1), Cond,
                         Op->getFlags(), CC, DL, DAG);
  return DAG.getNode(RXISD::SELECT_CC, DL, Op.getValueType(),
                     Op.getOperand(2), Op.getOperand(3),
                     DAG.getConstant(CC, DL, MVT::i32), Glue);
}

//===----------------------------------------------------------------------===//
// Calling convention
//===----------------------------------------------------------------------===//

#define GET_CALLING_CONV_IMPL
#include "RXGenCallingConv.inc"

template <typename ArgT> static bool isNamedArg(const ArgT &Arg) {
  return !Arg.Flags.isVarArg();
}

// Assigns argument locations the way GCC does (rx_function_arg and
// rx_function_arg_advance): a running byte count over R1-R4; an argument
// goes in registers only if it fits entirely, and the count advances for
// every argument, so once one argument spills, all later ones are on the
// stack too. In a variadic function the unnamed arguments and the last
// named one are on the stack. Stack arguments are naturally aligned (4 bytes
// for every scalar, including i64 and double) and packed without padding.
// An sret pointer is passed in R15 and does not count.
//
// Structures: the frontend passes one that GCC would put in registers (size
// a multiple of 4, at most 16 bytes) as integer parts, e.g. [N x i32], which
// are kept together like an i64; any other structure is passed byval. A
// byval argument is copied to the stack at its own alignment (a 3-byte char
// struct may be followed by another at offset 3) and advances the register
// count by its size rounded up to 4, so later arguments may still go in
// registers.
template <typename ArgT>
static void analyzeRXArguments(CCState &State,
                               const SmallVectorImpl<ArgT> &Args) {
  static const MCPhysReg ArgRegs[] = {RX::R1, RX::R2, RX::R3, RX::R4};
  const unsigned MaxRegBytes = 16;

  int LastNamed = -1;
  if (State.isVarArg())
    for (const ArgT &A : Args)
      if (isNamedArg(A))
        LastNamed = std::max<int>(LastNamed, A.OrigArgIndex);

  unsigned Bytes = 0;
  for (unsigned I = 0, E = Args.size(); I != E;) {
    // Parts [I, J) belong to one original argument.
    unsigned J = I + 1;
    while (J != E && Args[J].OrigArgIndex == Args[I].OrigArgIndex)
      ++J;
    ISD::ArgFlagsTy Flags = Args[I].Flags;
    if (Flags.isByVal()) {
      assert(J == I + 1 && "byval pointer split into parts");
      unsigned Size = Flags.getByValSize();
      unsigned Offset =
          State.AllocateStack(Size, Flags.getNonZeroByValAlign());
      State.addLoc(CCValAssign::getMem(I, Args[I].VT, Offset, Args[I].VT,
                                       CCValAssign::Full));
      Bytes += alignTo(Size, 4);
      I = J;
      continue;
    }

    if (Flags.isSRet()) {
      assert(J == I + 1 && "sret pointer split into parts");
      State.addLoc(CCValAssign::getReg(I, Args[I].VT, RX::R15, Args[I].VT,
                                       CCValAssign::Full));
      I = J;
      continue;
    }

    unsigned Size = 0;
    for (unsigned K = I; K != J; ++K)
      Size += alignTo(Args[K].VT.getStoreSize(), 4);

    bool Named = isNamedArg(Args[I]) &&
                 static_cast<int>(Args[I].OrigArgIndex) != LastNamed;
    if (Named && Bytes + Size <= MaxRegBytes) {
      unsigned Reg = Bytes / 4;
      for (unsigned K = I; K != J; ++K) {
        State.addLoc(CCValAssign::getReg(K, Args[K].VT, ArgRegs[Reg++],
                                         Args[K].VT, CCValAssign::Full));
        State.AllocateReg(ArgRegs[Reg - 1]);
      }
    } else {
      unsigned Offset = State.AllocateStack(Size, Align(4));
      for (unsigned K = I; K != J; ++K) {
        State.addLoc(CCValAssign::getMem(K, Args[K].VT, Offset, Args[K].VT,
                                         CCValAssign::Full));
        Offset += alignTo(Args[K].VT.getStoreSize(), 4);
      }
    }
    Bytes += Size;
    I = J;
  }
}

SDValue RXTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  if (CallConv != CallingConv::C && CallConv != CallingConv::Fast)
    report_fatal_error("RX: unsupported calling convention");

  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineRegisterInfo &RegInfo = MF.getRegInfo();

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());
  analyzeRXArguments(CCInfo, Ins);

  for (const CCValAssign &VA : ArgLocs) {
    const ISD::ArgFlagsTy &Flags = Ins[VA.getValNo()].Flags;
    SDValue ArgValue;
    if (VA.isRegLoc()) {
      Register VReg = RegInfo.createVirtualRegister(&RX::GPRRegClass);
      RegInfo.addLiveIn(VA.getLocReg(), VReg);
      ArgValue = DAG.getCopyFromReg(Chain, DL, VReg, VA.getLocVT());
      // GCC returns the sret pointer in R1; keep it for LowerReturn.
      if (Flags.isSRet())
        MF.getInfo<RXMachineFunctionInfo>()->setSRetReturnReg(VReg);
    } else if (Flags.isByVal()) {
      // The caller's copy is the argument: pass its address.
      int FI = MFI.CreateFixedObject(Flags.getByValSize(),
                                     VA.getLocMemOffset(),
                                     /*IsImmutable=*/false);
      ArgValue = DAG.getFrameIndex(FI, MVT::i32);
    } else {
      assert(VA.isMemLoc() && "unexpected argument location");
      unsigned Size = VA.getLocVT().getStoreSize();
      int FI = MFI.CreateFixedObject(Size, VA.getLocMemOffset(),
                                     /*IsImmutable=*/true);
      SDValue FIN = DAG.getFrameIndex(FI, MVT::i32);
      ArgValue = DAG.getLoad(VA.getLocVT(), DL, Chain, FIN,
                             MachinePointerInfo::getFixedStack(MF, FI));
    }

    if (VA.getLocInfo() == CCValAssign::SExt)
      ArgValue = DAG.getNode(ISD::AssertSext, DL, VA.getLocVT(), ArgValue,
                             DAG.getValueType(VA.getValVT()));
    else if (VA.getLocInfo() == CCValAssign::ZExt)
      ArgValue = DAG.getNode(ISD::AssertZext, DL, VA.getLocVT(), ArgValue,
                             DAG.getValueType(VA.getValVT()));
    if (VA.getLocInfo() != CCValAssign::Full)
      ArgValue = DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), ArgValue);
    InVals.push_back(ArgValue);
  }
  return Chain;
}

static SDValue promoteValue(SDValue V, const CCValAssign &VA,
                            const SDLoc &DL, SelectionDAG &DAG) {
  switch (VA.getLocInfo()) {
  case CCValAssign::Full:
    return V;
  case CCValAssign::SExt:
    return DAG.getNode(ISD::SIGN_EXTEND, DL, VA.getLocVT(), V);
  case CCValAssign::ZExt:
    return DAG.getNode(ISD::ZERO_EXTEND, DL, VA.getLocVT(), V);
  case CCValAssign::AExt:
    return DAG.getNode(ISD::ANY_EXTEND, DL, VA.getLocVT(), V);
  default:
    llvm_unreachable("unexpected location info");
  }
}

SDValue RXTargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                                    SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  SDLoc &DL = CLI.DL;
  SDValue Chain = CLI.Chain;
  SDValue Callee = CLI.Callee;
  CallingConv::ID CallConv = CLI.CallConv;
  MachineFunction &MF = DAG.getMachineFunction();

  if (CallConv != CallingConv::C && CallConv != CallingConv::Fast)
    report_fatal_error("RX: unsupported calling convention");
  CLI.IsTailCall = false;

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, CLI.IsVarArg, MF, ArgLocs, *DAG.getContext());
  analyzeRXArguments(CCInfo, CLI.Outs);
  unsigned NumBytes = CCInfo.getStackSize();

  Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, DL);

  SmallVector<std::pair<Register, SDValue>, 4> RegsToPass;
  SmallVector<SDValue, 8> MemOpChains;
  SDValue StackPtr;
  for (unsigned I = 0, E = ArgLocs.size(); I != E; ++I) {
    const CCValAssign &VA = ArgLocs[I];
    const ISD::ArgFlagsTy &Flags = CLI.Outs[I].Flags;
    SDValue Arg = promoteValue(CLI.OutVals[I], VA, DL, DAG);

    if (VA.isRegLoc()) {
      RegsToPass.push_back({VA.getLocReg(), Arg});
      continue;
    }
    assert(VA.isMemLoc() && "unexpected argument location");
    if (!StackPtr.getNode())
      StackPtr = DAG.getCopyFromReg(Chain, DL, RX::R0, MVT::i32);
    SDValue PtrOff =
        DAG.getNode(ISD::ADD, DL, MVT::i32, StackPtr,
                    DAG.getIntPtrConstant(VA.getLocMemOffset(), DL));
    if (Flags.isByVal()) {
      // Copy the structure into the outgoing argument area. Inline the copy:
      // there may be no memcpy to call.
      SDValue Size = DAG.getConstant(Flags.getByValSize(), DL, MVT::i32);
      Align A = Flags.getNonZeroByValAlign();
      MemOpChains.push_back(DAG.getMemcpy(
          Chain, DL, PtrOff, Arg, Size, A, A, /*isVolatile=*/false,
          /*AlwaysInline=*/true, /*CI=*/nullptr, std::nullopt,
          MachinePointerInfo(), MachinePointerInfo()));
      continue;
    }
    MemOpChains.push_back(
        DAG.getStore(Chain, DL, Arg, PtrOff, MachinePointerInfo()));
  }
  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, MemOpChains);

  SDValue Glue;
  for (const auto &[Reg, Val] : RegsToPass) {
    Chain = DAG.getCopyToReg(Chain, DL, Reg, Val, Glue);
    Glue = Chain.getValue(1);
  }

  if (auto *G = dyn_cast<GlobalAddressSDNode>(Callee))
    Callee = DAG.getTargetGlobalAddress(G->getGlobal(), DL, MVT::i32,
                                        G->getOffset());
  else if (auto *S = dyn_cast<ExternalSymbolSDNode>(Callee))
    Callee = DAG.getTargetExternalSymbol(S->getSymbol(), MVT::i32);

  SmallVector<SDValue, 8> Ops = {Chain, Callee};
  for (const auto &[Reg, Val] : RegsToPass)
    Ops.push_back(DAG.getRegister(Reg, Val.getValueType()));
  const TargetRegisterInfo *TRI = STI.getRegisterInfo();
  Ops.push_back(DAG.getRegisterMask(TRI->getCallPreservedMask(MF, CallConv)));
  if (Glue.getNode())
    Ops.push_back(Glue);

  Chain = DAG.getNode(RXISD::CALL, DL, DAG.getVTList(MVT::Other, MVT::Glue),
                      Ops);
  Glue = Chain.getValue(1);

  Chain = DAG.getCALLSEQ_END(Chain, NumBytes, 0, Glue, DL);
  Glue = Chain.getValue(1);

  // Copy the results out of their physical registers.
  SmallVector<CCValAssign, 4> RVLocs;
  CCState RetInfo(CallConv, CLI.IsVarArg, MF, RVLocs, *DAG.getContext());
  RetInfo.AnalyzeCallResult(CLI.Ins, RetCC_RX);
  for (const CCValAssign &VA : RVLocs) {
    SDValue Val =
        DAG.getCopyFromReg(Chain, DL, VA.getLocReg(), VA.getLocVT(), Glue);
    Chain = Val.getValue(1);
    Glue = Val.getValue(2);
    if (VA.getLocInfo() != CCValAssign::Full)
      Val = DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), Val);
    InVals.push_back(Val);
  }
  return Chain;
}

bool RXTargetLowering::CanLowerReturn(
    CallingConv::ID CallConv, MachineFunction &MF, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs, LLVMContext &Context,
    const Type *RetTy) const {
  SmallVector<CCValAssign, 4> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, RVLocs, Context);
  return CCInfo.CheckReturn(Outs, RetCC_RX);
}

SDValue
RXTargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                              bool IsVarArg,
                              const SmallVectorImpl<ISD::OutputArg> &Outs,
                              const SmallVectorImpl<SDValue> &OutVals,
                              const SDLoc &DL, SelectionDAG &DAG) const {
  SmallVector<CCValAssign, 4> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeReturn(Outs, RetCC_RX);

  SDValue Glue;
  SmallVector<SDValue, 4> RetOps(1, Chain);
  for (unsigned I = 0, E = RVLocs.size(); I != E; ++I) {
    const CCValAssign &VA = RVLocs[I];
    SDValue Val = promoteValue(OutVals[I], VA, DL, DAG);
    Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), Val, Glue);
    Glue = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  // Like GCC, return the address of an sret result in R1.
  MachineFunction &MF = DAG.getMachineFunction();
  if (Register SRet = MF.getInfo<RXMachineFunctionInfo>()->getSRetReturnReg()) {
    assert(RVLocs.empty() && "sret function returning a value");
    SDValue Val = DAG.getCopyFromReg(Chain, DL, SRet, MVT::i32);
    Chain = DAG.getCopyToReg(Chain, DL, RX::R1, Val, Glue);
    Glue = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(RX::R1, MVT::i32));
  }

  RetOps[0] = Chain;
  if (Glue.getNode())
    RetOps.push_back(Glue);
  return DAG.getNode(RXISD::RET_GLUE, DL, MVT::Other, RetOps);
}

//===----------------------------------------------------------------------===//
// Inline assembly
//===----------------------------------------------------------------------===//

TargetLowering::ConstraintType
RXTargetLowering::getConstraintType(StringRef Constraint) const {
  if (Constraint == "r")
    return C_RegisterClass;
  return TargetLowering::getConstraintType(Constraint);
}

std::pair<unsigned, const TargetRegisterClass *>
RXTargetLowering::getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                                               StringRef Constraint,
                                               MVT VT) const {
  // "r": any general-purpose register (R0 is SP and stays reserved).
  if (Constraint == "r")
    return {0U, &RX::GPRRegClass};
  // "{r1}", "{psw}", ... are resolved by name.
  return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
}

//===----------------------------------------------------------------------===//
// Custom inserters
//===----------------------------------------------------------------------===//

MachineBasicBlock *
RXTargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                              MachineBasicBlock *BB) const {
  assert(MI.getOpcode() == RX::SELECT && "unexpected custom inserter");
  const TargetInstrInfo &TII = *BB->getParent()->getSubtarget().getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();

  // ThisMBB:
  //   bcc CC, SinkMBB      ; take the true value
  // FalseMBB:
  // SinkMBB:
  //   rd = phi [t, ThisMBB], [f, FalseMBB]
  const BasicBlock *LLVMBB = BB->getBasicBlock();
  MachineFunction::iterator It = ++BB->getIterator();
  MachineFunction *F = BB->getParent();
  MachineBasicBlock *ThisMBB = BB;
  MachineBasicBlock *FalseMBB = F->CreateMachineBasicBlock(LLVMBB);
  MachineBasicBlock *SinkMBB = F->CreateMachineBasicBlock(LLVMBB);
  F->insert(It, FalseMBB);
  F->insert(It, SinkMBB);

  SinkMBB->splice(SinkMBB->begin(), BB,
                  std::next(MachineBasicBlock::iterator(MI)), BB->end());
  SinkMBB->transferSuccessorsAndUpdatePHIs(BB);

  BB->addSuccessor(FalseMBB);
  BB->addSuccessor(SinkMBB);
  BuildMI(BB, DL, TII.get(RX::BCC))
      .addMBB(SinkMBB)
      .addImm(MI.getOperand(3).getImm());

  FalseMBB->addSuccessor(SinkMBB);

  BuildMI(*SinkMBB, SinkMBB->begin(), DL, TII.get(TargetOpcode::PHI),
          MI.getOperand(0).getReg())
      .addReg(MI.getOperand(1).getReg())
      .addMBB(ThisMBB)
      .addReg(MI.getOperand(2).getReg())
      .addMBB(FalseMBB);

  MI.eraseFromParent();
  return SinkMBB;
}
