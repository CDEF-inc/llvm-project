//===- RXMCTargetDesc.h - RX Target Descriptions ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides RX specific target descriptions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RX_MCTARGETDESC_RXMCTARGETDESC_H
#define LLVM_LIB_TARGET_RX_MCTARGETDESC_RXMCTARGETDESC_H

#include <cstdint>
#include <memory>

namespace llvm {
class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCInstrInfo;
class MCObjectTargetWriter;
class MCRegisterInfo;
class MCStreamer;
class MCSubtargetInfo;
class MCTargetOptions;
class MCTargetStreamer;
class Target;

MCTargetStreamer *createRXObjectTargetStreamer(MCStreamer &S,
                                               const MCSubtargetInfo &STI);

MCCodeEmitter *createRXMCCodeEmitter(const MCInstrInfo &MCII, MCContext &Ctx);
MCAsmBackend *createRXMCAsmBackend(const Target &T, const MCSubtargetInfo &STI,
                                   const MCRegisterInfo &MRI,
                                   const MCTargetOptions &Options);
std::unique_ptr<MCObjectTargetWriter> createRXELFObjectWriter(uint8_t OSABI);
} // end namespace llvm

// Defines symbolic names for RX registers. This defines a mapping from
// register name to register number.
#define GET_REGINFO_ENUM
#include "RXGenRegisterInfo.inc"

// Defines symbolic names for the RX instructions.
#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_MC_HELPER_DECLS
#include "RXGenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "RXGenSubtargetInfo.inc"

#endif // LLVM_LIB_TARGET_RX_MCTARGETDESC_RXMCTARGETDESC_H
