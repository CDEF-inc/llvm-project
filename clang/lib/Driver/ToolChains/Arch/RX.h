//===--- RX.h - RX-specific Tool Helpers ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_ARCH_RX_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_ARCH_RX_H

#include "clang/Driver/Driver.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Option/ArgList.h"
#include <string>
#include <vector>

namespace clang {
namespace driver {
namespace tools {
namespace rx {

/// The CPU from -mcpu= (the RX backend's rxv1 / rxv2 / rxv3 / rxv3e, or
/// GCC's rx100 / rx200 / rx600 / rx610), else GNURX's -misa=v1|v2|v3|v3e,
/// else GCC's default rx600.
std::string getRXTargetCPU(const llvm::opt::ArgList &Args);

/// Whether the single-precision FPU is used (the CPU's default, changed by
/// -fpu / -mnofpu / -nofpu).
bool hasRXFPU(const llvm::opt::ArgList &Args);

/// Whether double is 64 bits (-m64bit-doubles, the default) or 32 bits
/// (-m32bit-doubles).
bool hasRX64BitDoubles(const llvm::opt::ArgList &Args);

void getRXTargetFeatures(const Driver &D, const llvm::opt::ArgList &Args,
                         std::vector<llvm::StringRef> &Features);

/// GNU as's -mcpu for the CPU and features.
llvm::StringRef getRXAsmCPU(const llvm::opt::ArgList &Args);

} // end namespace rx
} // end namespace tools
} // end namespace driver
} // end namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_ARCH_RX_H
