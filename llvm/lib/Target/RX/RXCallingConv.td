//===- RXCallingConv.td - Calling Conventions for RX -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The GCC rx-elf / GNURX calling convention (gcc/config/rx/rx.cc):
//   - arguments fill R1-R4 (16 bytes) by a running byte count; an argument
//     that does not fit entirely goes on the stack, and so does everything
//     after it (the count keeps growing). See analyzeRXArguments in
//     RXISelLowering.cpp, which implements this in C++.
//   - stack arguments are naturally aligned (4 bytes for i64/double)
//   - results in R1-R4 (i64/double: R1 low word, R2 high word); integer
//     results smaller than int are promoted
//   - R6-R13 are callee-saved
//
//===----------------------------------------------------------------------===//

def RetCC_RX : CallingConv<[
  CCIfType<[i1, i8, i16], CCPromoteToType<i32>>,
  CCIfType<[i32], CCAssignToReg<[R1, R2, R3, R4]>>
]>;

def CSR_RX : CalleeSavedRegs<(add R6, R7, R8, R9, R10, R11, R12, R13)>;
