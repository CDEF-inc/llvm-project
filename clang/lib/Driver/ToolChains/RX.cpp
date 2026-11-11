//===--- RX.cpp - Renesas RX ToolChain --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RX.h"
#include "clang/Driver/CommonArgs.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/InputInfo.h"
#include "clang/Options/Options.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/VirtualFileSystem.h"

using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

RXToolChain::RXToolChain(const Driver &D, const llvm::Triple &Triple,
                         const ArgList &Args)
    : Generic_ELF(D, Triple, Args) {
  GCCInstallation.init(Triple, Args);
  std::string MultilibSuf;
  if (GCCInstallation.isValid()) {
    MultilibSuf = GCCInstallation.getMultilib().gccSuffix();
    // <prefix>/rx-elf/bin has as, ld and ld.bfd.
    PushPPaths(getProgramPaths());
    addPathIfExists(D, GCCInstallation.getInstallPath() + MultilibSuf,
                    getFilePaths());
  }
  std::string SysRoot = computeSysRoot();
  addPathIfExists(D, SysRoot + "/lib" + MultilibSuf, getFilePaths());
  if (!MultilibSuf.empty())
    addPathIfExists(D, SysRoot + "/lib", getFilePaths());
}

std::string RXToolChain::computeSysRoot() const {
  if (!getDriver().SysRoot.empty())
    return getDriver().SysRoot;
  SmallString<128> Dir;
  if (GCCInstallation.isValid())
    llvm::sys::path::append(Dir, GCCInstallation.getParentLibPath(), "..",
                            GCCInstallation.getTriple().str());
  else
    llvm::sys::path::append(Dir, getDriver().Dir, "..", "rx-elf");
  return std::string(Dir);
}

void RXToolChain::AddClangSystemIncludeArgs(const ArgList &DriverArgs,
                                            ArgStringList &CC1Args) const {
  if (DriverArgs.hasArg(options::OPT_nostdinc, options::OPT_nostdlibinc))
    return;
  SmallString<128> Dir(computeSysRoot());
  llvm::sys::path::append(Dir, "include");
  if (getVFS().exists(Dir))
    addSystemInclude(DriverArgs, CC1Args, Dir.str());
}

void RXToolChain::addClangTargetOptions(const ArgList &DriverArgs,
                                        ArgStringList &CC1Args, BoundArch BA,
                                        Action::OffloadKind) const {
  // No host headers (/usr/include); clang's own and the C library's only.
  CC1Args.push_back("-nostdsysteminc");
}

Tool *RXToolChain::buildLinker() const { return new tools::rx::Linker(*this); }

// As rx-elf-gcc's specs: startfile "crt0.o crtbegin.o", lib "--start-group
// -lc -lnosys (or -lsim with -msim) --end-group" and the rx.ld (rx-sim.ld)
// script of libgloss unless -T is given, libgcc "-lgcc", endfile "crtend.o
// crtn.o".
void rx::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                              const InputInfo &Output,
                              const InputInfoList &Inputs, const ArgList &Args,
                              const char *LinkingOutput) const {
  const ToolChain &TC = getToolChain();
  const Driver &D = TC.getDriver();
  ArgStringList CmdArgs;

  bool UseStartFiles =
      !Args.hasArg(options::OPT_nostdlib, options::OPT_r,
                   options::OPT_nostartfiles);
  bool UseDefaultLibs = !Args.hasArg(options::OPT_nostdlib, options::OPT_r,
                                     options::OPT_nodefaultlibs);

  if (Args.hasArg(options::OPT_r))
    CmdArgs.push_back("-r");
  Args.addAllArgs(CmdArgs, {options::OPT_s, options::OPT_t, options::OPT_u});

  if (UseStartFiles) {
    CmdArgs.push_back(Args.MakeArgString(TC.GetFilePath("crt0.o")));
    CmdArgs.push_back(Args.MakeArgString(TC.GetFilePath("crtbegin.o")));
  }

  Args.AddAllArgs(CmdArgs, options::OPT_L);
  TC.AddFilePathLibArgs(Args, CmdArgs);
  AddLinkerInputs(TC, Inputs, Args, CmdArgs, JA);

  if (UseDefaultLibs) {
    if (D.CCCIsCXX() && TC.ShouldLinkCXXStdlib(Args))
      TC.AddCXXStdlibLibArgs(Args, CmdArgs);
    AddRunTimeLibs(TC, D, CmdArgs, Args);
    if (!Args.hasArg(options::OPT_nolibc)) {
      bool Sim = Args.hasArg(options::OPT_msim);
      CmdArgs.push_back("--start-group");
      CmdArgs.push_back("-lc");
      CmdArgs.push_back(Sim ? "-lsim" : "-lnosys");
      CmdArgs.push_back("--end-group");
      AddRunTimeLibs(TC, D, CmdArgs, Args);
      // libgloss's linker script, when the C library has one.
      if (!Args.hasArg(options::OPT_T)) {
        std::string Script = TC.GetFilePath(Sim ? "rx-sim.ld" : "rx.ld");
        if (llvm::sys::fs::exists(Script))
          CmdArgs.push_back(Args.MakeArgString("-T" + Script));
      }
    }
  }

  if (UseStartFiles) {
    CmdArgs.push_back(Args.MakeArgString(TC.GetFilePath("crtend.o")));
    CmdArgs.push_back(Args.MakeArgString(TC.GetFilePath("crtn.o")));
  }

  Args.AddAllArgs(CmdArgs, options::OPT_T);
  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  C.addCommand(std::make_unique<Command>(
      JA, *this, ResponseFileSupport::AtFileCurCP(),
      Args.MakeArgString(TC.GetLinkerPath()), CmdArgs, Inputs, Output));
}
