//===- RXRegisterInfo.td - RX Register defs ----------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Declarations that describe the RX register file
//===----------------------------------------------------------------------===//

class RXReg<bits<4> Enc, string n> : Register<n> {
  let HWEncoding{3-0} = Enc;
  let Namespace = "RX";
}

// DWARF numbers follow GCC rx-elf (rx_dbx_register_number in GNURX):
// R0-R15 = 0-15, PSW(cc) = 16, PC = 17, DRn = 32 + n.

// General-purpose registers. R0 is the stack pointer (USP/ISP).
foreach I = 0-15 in
  def R#I : RXReg<I, "r"#I>, DwarfRegNum<[I]>;

// Control registers (MVTC/MVFC/PUSHC/POPC operand; HWEncoding = cr number).
def PSW   : RXReg<0, "psw">, DwarfRegNum<[16]>;
def PC    : RXReg<1, "pc">, DwarfRegNum<[17]>;
def USP   : RXReg<2, "usp">;
def FPSW  : RXReg<3, "fpsw">;
def BPSW  : RXReg<8, "bpsw">;
def BPC   : RXReg<9, "bpc">;
def ISP   : RXReg<10, "isp">;
def FINTV : RXReg<11, "fintv">;
def INTB  : RXReg<12, "intb">;
def EXTB  : RXReg<13, "extb">;

// DSP accumulators (RXv2+).
def ACC0 : RXReg<0, "a0">;
def ACC1 : RXReg<1, "a1">;

// Double-precision FPU registers (RXv3 DFPU). GNURX -mdfpu uses them for
// arithmetic only: f64 arguments and return values still go in integer
// register pairs, and all DRn are call-clobbered.
foreach I = 0-15 in
  def DR#I : RXReg<I, "dr"#I>, DwarfRegNum<[!add(32, I)]>;

// Allocation order: caller-saved registers first, then callee-saved; R0 (SP)
// is reserved.
def GPR : RegisterClass<"RX", [i32], 32,
                        (add R1, R2, R3, R4, R5, R14, R15,
                             R6, R7, R8, R9, R10, R11, R12, R13, R0)>;

// R0-R7: the only registers the 2-byte "dsp5[Rs]" MOV/MOVU forms can encode.
def GPRLow : RegisterClass<"RX", [i32], 32, (sequence "R%u", 0, 7)>;

def DPR : RegisterClass<"RX", [f64], 64, (sequence "DR%u", 0, 15)>;

def CCR : RegisterClass<"RX", [i32], 32, (add PSW, FPSW)> {
  let isAllocatable = 0;
}

def CTRL : RegisterClass<"RX", [i32], 32,
                         (add PSW, PC, USP, FPSW, BPSW, BPC, ISP, FINTV,
                              INTB, EXTB)> {
  let isAllocatable = 0;
}

def ACC : RegisterClass<"RX", [i64], 64, (add ACC0, ACC1)> {
  let isAllocatable = 0;
}
