//===- RX.td - Describe the RX Target Machine --------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

//===----------------------------------------------------------------------===//
// RX Subtarget features
//===----------------------------------------------------------------------===//

def FeatureRXv2
    : SubtargetFeature<"rxv2", "HasRXv2", "true",
                       "Enable RXv2 instructions">;
def FeatureRXv3
    : SubtargetFeature<"rxv3", "HasRXv3", "true",
                       "Enable RXv3 instructions", [FeatureRXv2]>;
def FeatureDFPU
    : SubtargetFeature<"dfpu", "HasDFPU", "true",
                       "Enable double-precision FPU (DR0-DR15)">;
// GCC -m64bit-doubles: double is 64 bits. Recorded in e_flags; GNU ld
// refuses to mix objects that disagree.
def Feature64BitDoubles
    : SubtargetFeature<"64bit-doubles", "Has64BitDoubles", "true",
                       "double is 64 bits (GCC -m64bit-doubles)">;
// RXv3e (RX74M/RX74N) is upward compatible with RXv3 and adds Protect Zone.
// Its new instructions are not modelled yet; see the RXv3e ISA manual
// (R01US0745EJ).
def FeatureRXv3e
    : SubtargetFeature<"rxv3e", "HasRXv3e", "true",
                       "Enable RXv3e instructions", [FeatureRXv3]>;

def HasRXv2 : Predicate<"Subtarget->hasRXv2()">,
              AssemblerPredicate<(all_of FeatureRXv2), "RXv2">;
def HasRXv3 : Predicate<"Subtarget->hasRXv3()">,
              AssemblerPredicate<(all_of FeatureRXv3), "RXv3">;
def HasDFPU : Predicate<"Subtarget->hasDFPU()">,
              AssemblerPredicate<(all_of FeatureDFPU), "DFPU">;

//===----------------------------------------------------------------------===//
// Registers, instruction descriptions
//===----------------------------------------------------------------------===//

include "RXRegisterInfo.td"
include "RXCallingConv.td"
include "RXInstrInfo.td"

defm : RemapAllTargetPseudoPointerOperands<GPR>;

def RXInstrInfo : InstrInfo;

class Proc<string Name, list<SubtargetFeature> Features>
    : Processor<Name, NoItineraries, Features>;

def : Proc<"generic", []>;
def : Proc<"rxv1", []>;
def : Proc<"rxv2", [FeatureRXv2]>;
def : Proc<"rxv3", [FeatureRXv3, FeatureDFPU]>;
def : Proc<"rxv3e", [FeatureRXv3e, FeatureDFPU]>;

def RXAsmParser : AsmParser;

// Displacements and register ranges ("dsp[rN]", "r6-r13") are matched as
// separate tokens.
def RXAsmParserVariant : AsmParserVariant {
  let TokenizingCharacters = "[]-";
}

def RX : Target {
  let InstructionSet = RXInstrInfo;
  let AssemblyParsers = [RXAsmParser];
  let AssemblyParserVariants = [RXAsmParserVariant];
}
