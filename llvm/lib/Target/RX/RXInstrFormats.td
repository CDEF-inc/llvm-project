//===- RXInstrFormats.td - RX Instruction Formats ----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// RX instructions are 1 to 8 bytes long. The most significant byte of `Inst`
// is the first byte in memory: opcode bytes read naturally from left to
// right. Immediates and displacements are little-endian, so instruction
// definitions place their bytes swapped (low byte in the earlier position).

class RXInst<int sz, dag outs, dag ins, string asmstr, list<dag> pattern>
    : Instruction {
  let Namespace = "RX";
  let OutOperandList = outs;
  let InOperandList = ins;
  let AsmString = asmstr;
  let Pattern = pattern;
  let Size = sz;
  let DecoderNamespace = "RX";
}

class RXInst8<dag outs, dag ins, string asmstr, list<dag> pattern = []>
    : RXInst<1, outs, ins, asmstr, pattern> {
  field bits<8> Inst;
}

class RXInst16<dag outs, dag ins, string asmstr, list<dag> pattern = []>
    : RXInst<2, outs, ins, asmstr, pattern> {
  field bits<16> Inst;
}

class RXInst24<dag outs, dag ins, string asmstr, list<dag> pattern = []>
    : RXInst<3, outs, ins, asmstr, pattern> {
  field bits<24> Inst;
}

class RXInst32<dag outs, dag ins, string asmstr, list<dag> pattern = []>
    : RXInst<4, outs, ins, asmstr, pattern> {
  field bits<32> Inst;
}

class RXInst40<dag outs, dag ins, string asmstr, list<dag> pattern = []>
    : RXInst<5, outs, ins, asmstr, pattern> {
  field bits<40> Inst;
}

class RXInst48<dag outs, dag ins, string asmstr, list<dag> pattern = []>
    : RXInst<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
}

class RXInst56<dag outs, dag ins, string asmstr, list<dag> pattern = []>
    : RXInst<7, outs, ins, asmstr, pattern> {
  field bits<56> Inst;
}

class RXInst64<dag outs, dag ins, string asmstr, list<dag> pattern = []>
    : RXInst<8, outs, ins, asmstr, pattern> {
  field bits<64> Inst;
}
