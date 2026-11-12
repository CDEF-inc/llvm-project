//===--- RX.h - Declare RX target feature support ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the Renesas RX TargetInfo: the types and predefined
// macros of GCC for rx-elf, by default with -m64bit-doubles (the ABI of the
// RX backend and of rustc's rx-none-elf). GCC's -m32bit-doubles is the
// 64bit-doubles feature turned off.
//
// Two calling conventions (-mabi=): "ccrx", the default, is CC-RX's (CC-RX
// manual 9.1), and "gnu" is GNURX's (rx-elf-gcc). They differ in which
// registers the arguments after a stacked one take (CodeGen/Targets/RX.cpp)
// and in wchar_t: unsigned short for CC-RX, long for GNURX.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_RX_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_RX_H

#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"

namespace clang {
namespace targets {

class LLVM_LIBRARY_VISIBILITY RXTargetInfo : public TargetInfo {
  static const char *const GCCRegNames[];

  std::string CPU;
  std::string ABI = "ccrx";
  bool HasFPU = false;
  bool HasDFPU = false;

public:
  RXTargetInfo(const llvm::Triple &Triple, const TargetOptions &)
      : TargetInfo(Triple) {
    TLSSupported = false;
    // 64-bit types are 4-byte aligned (BIGGEST_ALIGNMENT is 32), and double
    // and long double are 64 bits.
    LongLongAlign = 32;
    DoubleAlign = LongDoubleAlign = 32;
    LongDoubleWidth = 64;
    LongDoubleFormat = &llvm::APFloat::IEEEdouble();
    SuitableAlign = 32;
    SizeType = UnsignedLong;
    PtrDiffType = SignedLong;
    IntPtrType = SignedLong;
    WCharType = UnsignedShort; // setABI
    WIntType = UnsignedInt;
    Char32Type = UnsignedLong;
    // C symbols carry a leading underscore (GCC's USER_LABEL_PREFIX), as the
    // data layout's `m:o` mangling says.
    UserLabelPrefix = "_";
    resetDataLayout();
    MaxAtomicPromoteWidth = MaxAtomicInlineWidth = 32;
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

  llvm::SmallVector<Builtin::InfosShard> getTargetBuiltins() const override {
    return {};
  }

  // As GCC's rx-elf (newlib's types): int32_t and int_least32_t are long,
  // int_fast8_t to int_fast32_t are int.
  IntType getIntTypeByWidth(unsigned BitWidth, bool IsSigned) const override {
    if (BitWidth == 32)
      return IsSigned ? SignedLong : UnsignedLong;
    return TargetInfo::getIntTypeByWidth(BitWidth, IsSigned);
  }
  IntType getLeastIntTypeByWidth(unsigned BitWidth,
                                 bool IsSigned) const override {
    if (BitWidth == 32)
      return IsSigned ? SignedLong : UnsignedLong;
    return TargetInfo::getLeastIntTypeByWidth(BitWidth, IsSigned);
  }
  IntType getFastIntTypeByWidth(unsigned BitWidth,
                                bool IsSigned) const override {
    if (BitWidth <= 32)
      return IsSigned ? SignedInt : UnsignedInt;
    return TargetInfo::getFastIntTypeByWidth(BitWidth, IsSigned);
  }

  StringRef getABI() const override { return ABI; }
  bool setABI(const std::string &Name) override {
    if (Name != "ccrx" && Name != "gnu")
      return false;
    ABI = Name;
    WCharType = Name == "gnu" ? SignedLong : UnsignedShort;
    return true;
  }

  bool isValidCPUName(StringRef Name) const override;
  void fillValidCPUList(SmallVectorImpl<StringRef> &Values) const override;
  bool setCPU(StringRef Name) override;

  bool initFeatureMap(llvm::StringMap<bool> &Features,
                      DiagnosticsEngine &Diags, StringRef CPU,
                      const std::vector<std::string> &FeaturesVec)
      const override;
  bool handleTargetFeatures(std::vector<std::string> &Features,
                            DiagnosticsEngine &Diags) override;
  bool isValidFeatureName(StringRef Name) const override;
  bool hasFeature(StringRef Feature) const override;

  ArrayRef<const char *> getGCCRegNames() const override;

  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override {
    static const TargetInfo::GCCRegAlias GCCRegAliases[] = {{{"sp"}, "r0"}};
    return llvm::ArrayRef(GCCRegAliases);
  }

  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &Info) const override {
    switch (*Name) {
    case 'r':
      Info.setAllowsRegister();
      return true;
    }
    return false;
  }

  std::string_view getClobbers() const override { return ""; }

  // GCC aligns long long and double to 4 bytes everywhere, and its MS
  // bit-field layout (the RX default) keeps the types' alignment.
  bool allowsLargerPreferedTypeAlignment() const override { return false; }
  bool msStructAlignsFieldsToTypeSize() const override { return false; }

  BuiltinVaListKind getBuiltinVaListKind() const override {
    return TargetInfo::VoidPtrBuiltinVaList;
  }
};

} // namespace targets
} // namespace clang
#endif // LLVM_CLANG_LIB_BASIC_TARGETS_RX_H
