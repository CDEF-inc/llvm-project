//===--- RX.cpp - RX Helpers for Tools --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RX.h"
#include "clang/Basic/DiagnosticDriver.h"
#include "clang/Options/Options.h"
#include "llvm/ADT/StringSwitch.h"

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

namespace {
struct RXCPU {
  // 1, 2, 3, or 4 for RXv3e; 0 for an unknown CPU (which cc1 rejects).
  unsigned ISA;
  bool FPU;
};
} // namespace

// As Basic/Targets/RX.cpp: the backend's CPUs and GCC's / GNURX's names.
static RXCPU getRXCPU(StringRef CPU) {
  return llvm::StringSwitch<RXCPU>(CPU)
      .Cases({"generic", "rxv1", "rx100", "rx200"}, {1, false})
      .Cases({"rx13t", "rx600", "rx610"}, {1, true})
      .Cases({"rxv2", "rx140", "rx230", "rx64m", "rx71m"}, {2, true})
      .Cases({"rxv3", "rx66t", "rx72t"}, {3, true})
      .Case("rxv3e", {4, true})
      .Default({0, false});
}

std::string rx::getRXTargetCPU(const ArgList &Args) {
  if (const Arg *A = Args.getLastArg(options::OPT_mcpu_EQ))
    return A->getValue();
  if (const Arg *A = Args.getLastArg(options::OPT_misa_EQ))
    return llvm::StringSwitch<std::string>(A->getValue())
        .Case("v1", "rxv1")
        .Case("v2", "rxv2")
        .Case("v3", "rxv3")
        .Case("v3e", "rxv3e")
        .Default(std::string("rx") + A->getValue());
  return "rx600";
}

// GNURX's -misa= (without -mcpu=) keeps its default -fpu and uses the DFPU
// only with -mdfpu, unlike the backend's rxv1 (no FPU) and rxv3 (DFPU).
static bool cpuFromISA(const ArgList &Args) {
  return !Args.hasArg(options::OPT_mcpu_EQ) && Args.hasArg(options::OPT_misa_EQ);
}

bool rx::hasRXFPU(const ArgList &Args) {
  if (const Arg *A =
          Args.getLastArg(options::OPT_rx_fpu, options::OPT_mnofpu))
    return A->getOption().matches(options::OPT_rx_fpu);
  return cpuFromISA(Args) || getRXCPU(getRXTargetCPU(Args)).FPU;
}

bool rx::hasRX64BitDoubles(const ArgList &Args) {
  return Args.hasFlag(options::OPT_m64bit_doubles, options::OPT_m32bit_doubles,
                      true);
}

void rx::getRXTargetFeatures(const Driver &D, const ArgList &Args,
                             std::vector<StringRef> &Features) {
  std::string CPU = getRXTargetCPU(Args);
  RXCPU Info = getRXCPU(CPU);
  if (const Arg *A = Args.getLastArg(options::OPT_misa_EQ)) {
    if (!llvm::StringSwitch<bool>(A->getValue())
             .Cases({"v1", "v2", "v3", "v3e"}, true)
             .Default(false))
      D.Diag(diag::err_drv_unsupported_option_argument)
          << A->getSpelling() << A->getValue();
  }

  if (const Arg *A =
          Args.getLastArg(options::OPT_rx_fpu, options::OPT_mnofpu)) {
    if (A->getOption().matches(options::OPT_rx_fpu)) {
      // As GCC: "the RX200 cpu does not have FPU hardware".
      if (CPU == "rx100" || CPU == "rx200")
        D.Diag(diag::err_drv_argument_not_allowed_with)
            << A->getAsString(Args) << ("-mcpu=" + CPU);
      Features.push_back("+fpu");
    } else {
      Features.push_back("-fpu");
    }
  } else if (cpuFromISA(Args)) {
    Features.push_back("+fpu");
  }

  const Arg *DFPU = Args.getLastArg(options::OPT_mdfpu, options::OPT_mno_dfpu);
  if (DFPU && DFPU->getOption().matches(options::OPT_mdfpu)) {
    // As GNURX: "DFPU is not supported for this ISA/CPU" and "DFPU is not
    // supported with 32 bit doubles".
    if (Info.ISA && Info.ISA < 3)
      D.Diag(diag::err_drv_argument_not_allowed_with)
          << DFPU->getAsString(Args) << ("-mcpu=" + CPU);
    if (const Arg *A = Args.getLastArg(options::OPT_m32bit_doubles,
                                       options::OPT_m64bit_doubles);
        A && A->getOption().matches(options::OPT_m32bit_doubles))
      D.Diag(diag::err_drv_argument_not_allowed_with)
          << DFPU->getAsString(Args) << A->getAsString(Args);
    Features.push_back("+dfpu");
  } else if (DFPU || cpuFromISA(Args)) {
    Features.push_back("-dfpu");
  }

  Features.push_back(hasRX64BitDoubles(Args) ? "+64bit-doubles"
                                             : "-64bit-doubles");
  if (const Arg *A = Args.getLastArg(options::OPT_mrelax, options::OPT_mno_relax))
    Features.push_back(A->getOption().matches(options::OPT_mrelax) ? "+relax"
                                                                    : "-relax");
}

StringRef rx::getRXAsmCPU(const ArgList &Args) {
  std::string CPU = getRXTargetCPU(Args);
  RXCPU Info = getRXCPU(CPU);
  bool DFPU = Args.hasFlag(options::OPT_mdfpu, options::OPT_mno_dfpu,
                           !cpuFromISA(Args) &&
                               (CPU == "rxv3" || CPU == "rxv3e"));
  // GNU as knows rx100 / rx200 / rx600 / rx610 / rxv2 / rxv3 / rxv3-dfpu.
  if (Info.ISA >= 3)
    return DFPU ? "rxv3-dfpu" : "rxv3";
  if (Info.ISA == 2)
    return "rxv2";
  return llvm::StringSwitch<StringRef>(CPU)
      .Case("rx100", "rx100")
      .Case("rx200", "rx200")
      .Case("rx600", "rx600")
      .Case("rx610", "rx610")
      .Default("");
}
