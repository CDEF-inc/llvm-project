//===--- RX.h - Renesas RX ToolChain ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_RX_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_RX_H

#include "Gnu.h"
#include "clang/Driver/Tool.h"
#include "clang/Driver/ToolChain.h"

namespace clang {
namespace driver {
namespace toolchains {

/// rx-none-elf (bare metal). Uses an rx-elf GCC installation (found next to
/// clang, or with --gcc-toolchain / --gcc-install-dir) for libgcc, its
/// crtbegin.o / crtend.o and the C library (newlib) beside it, picking the
/// GCC multilib that matches -m64bit-doubles / -mnofpu. Links with ld.lld by
/// default; -fuse-ld=bfd picks the installation's GNU ld.
class LLVM_LIBRARY_VISIBILITY RXToolChain : public Generic_ELF {
public:
  RXToolChain(const Driver &D, const llvm::Triple &Triple,
              const llvm::opt::ArgList &Args);

  void
  AddClangSystemIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                            llvm::opt::ArgStringList &CC1Args) const override;
  void addClangTargetOptions(const llvm::opt::ArgList &DriverArgs,
                             llvm::opt::ArgStringList &CC1Args, BoundArch BA,
                             Action::OffloadKind) const override;

  bool isPICDefault() const override { return false; }
  bool isPIEDefault(const llvm::opt::ArgList &Args) const override {
    return false;
  }
  bool isPICDefaultForced() const override { return false; }
  bool IsIntegratedAssemblerDefault() const override { return true; }
  const char *getDefaultLinker() const override { return "ld.lld"; }
  RuntimeLibType GetDefaultRuntimeLibType() const override {
    return GCCInstallation.isValid() ? RLT_Libgcc : RLT_CompilerRT;
  }
  UnwindLibType
  GetUnwindLibType(const llvm::opt::ArgList &Args) const override {
    return UNW_None;
  }

  /// The C library's directory: --sysroot, else <prefix>/rx-elf of the GCC
  /// installation (or of clang's own directory).
  std::string computeSysRoot() const override;

protected:
  Tool *buildLinker() const override;
};

} // end namespace toolchains

namespace tools {
namespace rx {

class LLVM_LIBRARY_VISIBILITY Linker final : public Tool {
public:
  Linker(const ToolChain &TC) : Tool("RX::Linker", "ld.lld", TC) {}
  bool hasIntegratedCPP() const override { return false; }
  bool isLinkJob() const override { return true; }
  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};

} // end namespace rx
} // end namespace tools
} // end namespace driver
} // end namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_RX_H
