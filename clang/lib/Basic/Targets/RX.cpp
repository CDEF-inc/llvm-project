//===--- RX.cpp - Implement RX target feature support ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the RX TargetInfo.
//
//===----------------------------------------------------------------------===//

#include "RX.h"
#include "clang/Basic/MacroBuilder.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSwitch.h"

using namespace clang;
using namespace clang::targets;

const char *const RXTargetInfo::GCCRegNames[] = {
    "r0", "r1", "r2",  "r3",  "r4",  "r5",  "r6",  "r7",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"};

ArrayRef<const char *> RXTargetInfo::getGCCRegNames() const {
  return llvm::ArrayRef(GCCRegNames);
}

namespace {
struct RXCPUInfo {
  StringRef Name;
  // 1, 2, 3, or 4 for RXv3e.
  unsigned ISA;
  bool FPU;
  bool DFPU;
};
} // namespace

// The CPUs of the RX backend, and GCC's / GNURX's -mcpu names (rx100 and
// rx200 have no FPU, and none of them uses the DFPU unless -mdfpu). The FPU
// / DFPU defaults are the backend's.
static constexpr RXCPUInfo RXCPUs[] = {
    {"generic", 1, false, false}, {"rxv1", 1, false, false},
    {"rxv2", 2, true, false},     {"rxv3", 3, true, true},
    {"rxv3e", 4, true, true},     {"rx100", 1, false, false},
    {"rx13t", 1, true, false},    {"rx140", 2, true, false},
    {"rx200", 1, false, false},   {"rx230", 2, true, false},
    {"rx600", 1, true, false},    {"rx610", 1, true, false},
    {"rx64m", 2, true, false},    {"rx66t", 3, true, false},
    {"rx71m", 2, true, false},    {"rx72t", 3, true, false},
};

static const RXCPUInfo *findRXCPU(StringRef Name) {
  for (const RXCPUInfo &C : RXCPUs)
    if (C.Name == Name)
      return &C;
  return nullptr;
}

bool RXTargetInfo::isValidCPUName(StringRef Name) const {
  return findRXCPU(Name) != nullptr;
}

void RXTargetInfo::fillValidCPUList(SmallVectorImpl<StringRef> &Values) const {
  for (const RXCPUInfo &C : RXCPUs)
    Values.push_back(C.Name);
}

bool RXTargetInfo::setCPU(StringRef Name) {
  if (!isValidCPUName(Name))
    return false;
  CPU = Name.str();
  return true;
}

bool RXTargetInfo::initFeatureMap(
    llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags, StringRef CPU,
    const std::vector<std::string> &FeaturesVec) const {
  // double is 64 bits unless -m32bit-doubles turns the feature off.
  Features["64bit-doubles"] = true;
  if (const RXCPUInfo *C = findRXCPU(CPU)) {
    if (C->ISA >= 2)
      Features["rxv2"] = true;
    if (C->ISA >= 3)
      Features["rxv3"] = true;
    if (C->ISA >= 4)
      Features["rxv3e"] = true;
    if (C->FPU)
      Features["fpu"] = true;
    if (C->DFPU)
      Features["dfpu"] = true;
  }
  return TargetInfo::initFeatureMap(Features, Diags, CPU, FeaturesVec);
}

bool RXTargetInfo::handleTargetFeatures(std::vector<std::string> &Features,
                                        DiagnosticsEngine &Diags) {
  bool Has64BitDoubles = true;
  for (const std::string &F : Features) {
    bool On = F[0] == '+';
    StringRef Name = StringRef(F).drop_front();
    if (Name == "fpu")
      HasFPU = On;
    else if (Name == "dfpu")
      HasDFPU = On;
    else if (Name == "64bit-doubles")
      Has64BitDoubles = On;
  }
  if (!Has64BitDoubles) {
    // GCC's -m32bit-doubles: double and long double are float.
    DoubleWidth = LongDoubleWidth = 32;
    DoubleFormat = LongDoubleFormat = &llvm::APFloat::IEEEsingle();
  }
  return true;
}

bool RXTargetInfo::isValidFeatureName(StringRef Name) const {
  return llvm::StringSwitch<bool>(Name)
      .Cases({"rxv2", "rxv3", "rxv3e", "fpu", "dfpu", "64bit-doubles",
              "relax", "flush-denormals"},
             true)
      .Default(false);
}

bool RXTargetInfo::hasFeature(StringRef Feature) const {
  return llvm::StringSwitch<bool>(Feature)
      .Case("rx", true)
      .Case("fpu", HasFPU)
      .Case("dfpu", HasDFPU)
      .Case("64bit-doubles", DoubleWidth == 64)
      .Default(false);
}

void RXTargetInfo::getTargetDefines(const LangOptions &Opts,
                                    MacroBuilder &Builder) const {
  // As GNURX 14.2 (rx-elf-gcc) defines them (TARGET_CPU_CPP_BUILTINS in
  // gcc/config/rx/rx.h), for the default RX ABI and the GAS syntax.
  Builder.defineMacro("__RX__");
  // GCC's -mcpu names define their own macro, e.g. __RX600__; the backend's
  // (as GNURX's -misa=) do not. GNURX defines the ISA's macro, RXv3 for
  // RXv3e too.
  const RXCPUInfo *C = findRXCPU(CPU);
  if (C && !C->Name.starts_with("rxv") && C->Name != "generic")
    Builder.defineMacro("__" + C->Name.upper() + "__");
  unsigned ISA = C ? std::min(C->ISA, 3u) : 1;
  Builder.defineMacro("__RXv" + Twine(ISA) + "__");
  Builder.defineMacro("__RX_LITTLE_ENDIAN__");
  if (DoubleWidth == 64)
    Builder.defineMacro("__RX_64BIT_DOUBLES__");
  else
    Builder.defineMacro("__RX_32BIT_DOUBLES__");
  if (HasFPU)
    Builder.defineMacro("__RX_FPU_INSNS__");
  if (HasDFPU)
    Builder.defineMacro("__RX_DFPU_INSNS__");
  Builder.defineMacro("__RX_GAS_SYNTAX__");
  // Stacked arguments are naturally aligned (not GCC's -mgcc-abi).
  Builder.defineMacro("__RX_ABI__");
  Builder.defineMacro("__RX_ALLOW_STRING_INSNS__");
}
