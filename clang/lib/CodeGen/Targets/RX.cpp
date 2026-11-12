//===- RX.cpp -------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The Renesas RX C ABI: CC-RX's (the default, -mabi=ccrx; CC-RX manual
// 9.1) or GNURX's (-mabi=gnu; gcc/config/rx/rx.cc: rx_function_arg,
// rx_function_arg_advance, rx_function_arg_boundary, rx_return_in_memory,
// rx_promote_function_mode). rustc's rx-none-elf
// (compiler/rustc_target/src/callconv/rx.rs) makes the same choices for
// aggregates.
//
// Arguments fill R1-R4 by a byte count, each argument rounded up to 4 bytes;
// an argument goes in registers only if it fits entirely. CC-RX advances the
// count only for an argument placed in registers, so later ones still fill
// the free registers; GNURX advances it for every argument. In a variadic
// function the unnamed arguments and the last named one are on the stack.
// Stack arguments are at their natural alignment and size (a char takes one
// byte).
//
// The RX backend assigns the registers and stack slots with that count. The
// frontend decides only how an aggregate travels: a structure whose size is
// a multiple of 4 and that goes in registers is passed as [N x i32]; any
// other structure, and any structure on the stack, is passed byval at its
// natural alignment. Structures of 1-16 bytes whose size is a multiple of 4
// are returned in R1-R4, others in memory through a pointer in R15 (sret).
//
// char, short and _Bool arguments are extended by the caller (signext /
// zeroext), which CC-RX's callee relies on; the RX backend extends them again
// in the callee, since GNURX's caller does not. char and short return values
// are extended by the callee in both.
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "TargetInfo.h"

using namespace clang;
using namespace clang::CodeGen;

namespace {

class RXABIInfo : public DefaultABIInfo {
  /// Bytes of arguments that fit in R1-R4.
  static constexpr uint64_t ArgRegBytes = 16;

  /// GNURX's argument count (-mabi=gnu) instead of CC-RX's.
  bool GNUABI;

public:
  RXABIInfo(CodeGenTypes &CGT, bool GNUABI)
      : DefaultABIInfo(CGT), GNUABI(GNUABI) {}

  void computeInfo(CGFunctionInfo &FI) const override;
  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override;

private:
  ABIArgInfo classifyReturnType(QualType RetTy) const;
  ABIArgInfo classifyArgumentType(QualType Ty, bool CanUseRegs,
                                  uint64_t &UsedBytes) const;

  /// Whether GCC treats Ty as an aggregate (AGGREGATE_TYPE_P, or a vector,
  /// which RX has no registers for).
  bool isRXAggregate(QualType Ty) const {
    return isAggregateTypeForABI(Ty) || Ty->isVectorType();
  }

  /// Size bytes of integer registers: i8 / i16 / i32 or [N x i32].
  llvm::Type *getIntegerParts(uint64_t Size) const {
    llvm::LLVMContext &Ctx = getVMContext();
    if (Size < 4)
      return llvm::IntegerType::get(Ctx, Size * 8);
    llvm::Type *I32 = llvm::Type::getInt32Ty(Ctx);
    if (Size == 4)
      return I32;
    return llvm::ArrayType::get(I32, Size / 4);
  }

  ABIArgInfo getByVal(QualType Ty) const {
    return ABIArgInfo::getIndirect(getContext().getTypeAlignInChars(Ty),
                                   getDataLayout().getAllocaAddrSpace(),
                                   /*ByVal=*/true, /*Realign=*/false);
  }
};

class RXTargetCodeGenInfo : public TargetCodeGenInfo {
public:
  RXTargetCodeGenInfo(CodeGenTypes &CGT, bool GNUABI)
      : TargetCodeGenInfo(std::make_unique<RXABIInfo>(CGT, GNUABI)) {}
};

} // namespace

void RXABIInfo::computeInfo(CGFunctionInfo &FI) const {
  if (!getCXXABI().classifyReturnType(FI))
    FI.getReturnInfo() = classifyReturnType(FI.getReturnType());

  // The sret pointer goes in R15 and does not count. In a variadic function
  // the last named argument and the unnamed ones are on the stack.
  unsigned NumRegArgs = FI.arg_size();
  if (FI.isVariadic())
    NumRegArgs = FI.getNumRequiredArgs() ? FI.getNumRequiredArgs() - 1 : 0;
  uint64_t UsedBytes = 0;
  unsigned ArgNo = 0;
  for (auto &Arg : FI.arguments()) {
    Arg.info = classifyArgumentType(Arg.type, ArgNo < NumRegArgs, UsedBytes);
    ++ArgNo;
  }
}

ABIArgInfo RXABIInfo::classifyReturnType(QualType RetTy) const {
  if (RetTy->isVoidType())
    return ABIArgInfo::getIgnore();

  uint64_t Size = getContext().getTypeSize(RetTy) / 8;
  // _Complex is not an aggregate for GCC: it comes back in R1-R4 in its own
  // (unpromoted) mode, e.g. _Complex char in the low 16 bits of R1.
  if (RetTy->isAnyComplexType())
    return ABIArgInfo::getDirect(getIntegerParts(Size));

  if (isRXAggregate(RetTy)) {
    // Structures of 1-16 bytes whose size is a multiple of 4 come back in
    // R1-R4; others (including empty ones) in memory.
    if (Size >= 1 && Size <= ArgRegBytes && Size % 4 == 0)
      return ABIArgInfo::getDirect(getIntegerParts(Size));
    return getNaturalAlignIndirect(RetTy, getDataLayout().getAllocaAddrSpace(),
                                   /*ByVal=*/false);
  }

  if (const auto *EIT = RetTy->getAs<BitIntType>())
    if (EIT->getNumBits() > 64)
      return getNaturalAlignIndirect(
          RetTy, getDataLayout().getAllocaAddrSpace(), /*ByVal=*/false);

  // char and short return values are extended to int by the callee.
  return DefaultABIInfo::classifyReturnType(RetTy);
}

ABIArgInfo RXABIInfo::classifyArgumentType(QualType Ty, bool CanUseRegs,
                                           uint64_t &UsedBytes) const {
  Ty = useFirstFieldIfTransparentUnion(Ty);

  // Advances the register count as the backend does (analyzeRXArguments):
  // CC-RX counts only the arguments placed in registers, GNURX all.
  auto Count = [&](uint64_t Bytes, bool InRegs) {
    if (InRegs || GNUABI)
      UsedBytes += llvm::alignTo(Bytes, 4);
  };
  auto Fits = [&](uint64_t Bytes) {
    return CanUseRegs && UsedBytes + llvm::alignTo(Bytes, 4) <= ArgRegBytes;
  };

  // C++ objects that must be passed by reference take a pointer.
  if (CGCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI())) {
    Count(4, Fits(4));
    return getNaturalAlignIndirect(Ty, getDataLayout().getAllocaAddrSpace(),
                                   RAA == CGCXXABI::RAA_DirectInMemory);
  }

  uint64_t Size = getContext().getTypeSize(Ty) / 8;
  bool InRegs = Fits(Size);

  if (Ty->isAnyComplexType()) {
    // Not an aggregate for GCC: in registers whenever it fits (_Complex char
    // in the low 16 bits of a register), else on the stack at its natural
    // alignment.
    Count(Size, InRegs);
    if (InRegs)
      return ABIArgInfo::getDirect(getIntegerParts(Size));
    return getByVal(Ty);
  }

  if (isRXAggregate(Ty)) {
    // GCC passes an empty structure on the stack, where it takes no room.
    if (Size == 0)
      return ABIArgInfo::getIgnore();
    InRegs = InRegs && Size % 4 == 0;
    Count(Size, InRegs);
    if (InRegs)
      return ABIArgInfo::getDirect(getIntegerParts(Size));
    // On the stack at the type's own alignment: a 3-byte struct of chars
    // may be followed by another one at offset 3.
    return getByVal(Ty);
  }

  if (const auto *EIT = Ty->getAs<BitIntType>())
    if (EIT->getNumBits() > 64) {
      Count(Size, false);
      return getByVal(Ty);
    }

  // A scalar goes in registers whenever it fits (the backend decides).
  Count(Size, InRegs);
  if (const auto *ED = Ty->getAsEnumDecl())
    Ty = ED->getIntegerType();
  // char, short and _Bool are extended to int by the caller; on the stack
  // they still take their own size. _Bool travels as i8.
  if (Ty->isBooleanType())
    return ABIArgInfo::getExtend(Ty, llvm::Type::getInt8Ty(getVMContext()));
  if (isPromotableIntegerTypeForABI(Ty))
    return ABIArgInfo::getExtend(Ty);
  return ABIArgInfo::getDirect(CGT.ConvertType(Ty));
}

RValue RXABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                            QualType Ty, AggValueSlot Slot) const {
  // va_list points at the next unnamed argument. GCC's va_arg
  // (std_gimplify_va_arg_expr with PARM_BOUNDARY 8) aligns it to the type's
  // alignment and advances it by the type's size, as the caller laid them
  // out.
  bool IsIndirect = false;
  if (getRecordArgABI(Ty, getCXXABI()) == CGCXXABI::RAA_Indirect)
    IsIndirect = true;
  return emitVoidPtrVAArg(CGF, VAListAddr, Ty, IsIndirect,
                          getContext().getTypeInfoInChars(Ty),
                          CharUnits::One(), /*AllowHigherAlign=*/true, Slot);
}

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createRXTargetCodeGenInfo(CodeGenModule &CGM) {
  return std::make_unique<RXTargetCodeGenInfo>(
      CGM.getTypes(), CGM.getTarget().getABI() == "gnu");
}
