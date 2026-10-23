//===- RXInstrInfo.td - Target Description for RX ----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Many RX instructions have several encodings that differ only in the size
// of an immediate or displacement. Each encoding is a separate instruction
// here, and the operand classes form a subset chain so that the assembler
// matcher tries the shortest encoding first, making the same choice as
// GNU as (gas/config/rx-parse.y).
//
//===----------------------------------------------------------------------===//

include "RXInstrFormats.td"

//===----------------------------------------------------------------------===//
// Operands
//===----------------------------------------------------------------------===//

// Immediates written as "#expr". Values are taken modulo 2^32 the way GNU as
// does: 0x80000000-0xffffffff are treated as negative.
class RXImmClass<string name, list<AsmOperandClass> supers = []>
    : AsmOperandClass {
  let Name = name;
  let SuperClasses = supers;
  let RenderMethod = "addImmOperands";
}

def ImmS32Class   : RXImmClass<"ImmS32">;
def ImmS24Class   : RXImmClass<"ImmS24", [ImmS32Class]>;
def ImmS16Class   : RXImmClass<"ImmS16", [ImmS24Class]>;
def ImmS8Class    : RXImmClass<"ImmS8", [ImmS16Class]>;
// -128..-1: the simm8 values not already covered by the uimm8 forms of
// MOV and CMP.
def ImmS8NegClass : RXImmClass<"ImmS8Neg", [ImmS8Class]>;
def ImmU8Class    : RXImmClass<"ImmU8", [ImmS16Class]>;
def ImmU4Class    : RXImmClass<"ImmU4", [ImmS8Class, ImmU8Class]>;
def ImmU5Class    : RXImmClass<"ImmU5">;
def RtsdImmClass  : RXImmClass<"RtsdImm">;

def immu4   : Operand<i32> { let ParserMatchClass = ImmU4Class; }
def immu5   : Operand<i32> { let ParserMatchClass = ImmU5Class; }
def immu8   : Operand<i32> { let ParserMatchClass = ImmU8Class; }
def imms8   : Operand<i32> { let ParserMatchClass = ImmS8Class; }
def imms8n  : Operand<i32> { let ParserMatchClass = ImmS8NegClass; }
def imms16  : Operand<i32> { let ParserMatchClass = ImmS16Class; }
def imms24  : Operand<i32> { let ParserMatchClass = ImmS24Class; }
def imm32   : Operand<i32> {
  let ParserMatchClass = ImmS32Class;
  let EncoderMethod = "getImm32OpValue";
}
// RTSD stack adjustment in bytes (0..1020, multiple of 4); encoded / 4.
def rtsdimm : Operand<i32> {
  let ParserMatchClass = RtsdImmClass;
  let EncoderMethod = "getScaledOpValue<2>";
}

// Displacements written as "dsp[Rn]" (no '#'). The value is in bytes and
// is encoded divided by the access size.
class RXDspClass<string name, list<AsmOperandClass> supers = []>
    : AsmOperandClass {
  let Name = name;
  let SuperClasses = supers;
  let RenderMethod = "addImmOperands";
}

foreach S = ["B", "W", "L"] in {
  def Dsp16#S#Class : RXDspClass<"Dsp16"#S>;
  def Dsp8#S#Class  : RXDspClass<"Dsp8"#S,
                                 [!cast<AsmOperandClass>("Dsp16"#S#"Class")]>;
  def Dsp5#S#Class  : RXDspClass<"Dsp5"#S,
                                 [!cast<AsmOperandClass>("Dsp8"#S#"Class")]>;
}

class RXDsp<string cls, int shift> : Operand<i32> {
  let ParserMatchClass = !cast<AsmOperandClass>(cls#"Class");
  let EncoderMethod = "getScaledOpValue<"#shift#">";
  let PrintMethod = "printDisp";
}

foreach N = ["5", "8", "16"] in {
  def dsp#N#b : RXDsp<"Dsp"#N#"B", 0>;
  def dsp#N#w : RXDsp<"Dsp"#N#"W", 1>;
  def dsp#N#l : RXDsp<"Dsp"#N#"L", 2>;
}

// Branch targets (a label, or a constant PC-relative displacement).
def BrTargetClass : AsmOperandClass {
  let Name = "BrTarget";
  let RenderMethod = "addImmOperands";
}

class RXBrTarget<string encoder> : Operand<OtherVT> {
  let ParserMatchClass = BrTargetClass;
  let EncoderMethod = encoder;
  let PrintMethod = "printPCRel";
  let OperandType = "OPERAND_PCREL";
}

def brtarget3  : RXBrTarget<"getBrTarget3OpValue">;
def brtarget8  : RXBrTarget<"getBrTarget8OpValue">;
def brtarget16 : RXBrTarget<"getBrTarget16OpValue">;
def brtarget24 : RXBrTarget<"getBrTarget24OpValue">;

//===----------------------------------------------------------------------===//
// Data transfer: MOV / MOVU
//===----------------------------------------------------------------------===//

// sz field: 00 = .B, 01 = .W, 10 = .L
multiclass MovSized<string sfx, bits<2> sz, Operand dsp5, Operand dsp8,
                    Operand dsp16> {
  defvar mn = "mov."#sfx;

  // mov.sz Rs, Rd
  def _rr : RXInst16<(outs GPR:$rd), (ins GPR:$rs), mn#"\t$rs, $rd"> {
    bits<4> rs;
    bits<4> rd;
    let Inst{15-8} = {0b11, sz, 0b1111};
    let Inst{7-4} = rs;
    let Inst{3-0} = rd;
  }

  // Loads: mov.sz [Rs], Rd / dsp[Rs], Rd
  let mayLoad = 1 in {
  def _ld0 : RXInst16<(outs GPR:$rd), (ins GPR:$rs), mn#"\t[$rs], $rd"> {
    bits<4> rs;
    bits<4> rd;
    let Inst{15-8} = {0b11, sz, 0b1100};
    let Inst{7-4} = rs;
    let Inst{3-0} = rd;
  }
  def _ld5 : RXInst16<(outs GPRLow:$rd), (ins GPRLow:$rs, dsp5:$dsp),
                      mn#"\t$dsp[$rs], $rd"> {
    bits<3> rs;
    bits<3> rd;
    bits<5> dsp;
    let Inst{15-8} = {0b10, sz, 0b1, dsp{4-2}};
    let Inst{7} = dsp{1};
    let Inst{6-4} = rs;
    let Inst{3} = dsp{0};
    let Inst{2-0} = rd;
  }
  def _ld8 : RXInst24<(outs GPR:$rd), (ins GPR:$rs, dsp8:$dsp),
                      mn#"\t$dsp[$rs], $rd"> {
    bits<4> rs;
    bits<4> rd;
    bits<8> dsp;
    let Inst{23-16} = {0b11, sz, 0b1101};
    let Inst{15-12} = rs;
    let Inst{11-8} = rd;
    let Inst{7-0} = dsp;
  }
  def _ld16 : RXInst32<(outs GPR:$rd), (ins GPR:$rs, dsp16:$dsp),
                       mn#"\t$dsp[$rs], $rd"> {
    bits<4> rs;
    bits<4> rd;
    bits<16> dsp;
    let Inst{31-24} = {0b11, sz, 0b1110};
    let Inst{23-20} = rs;
    let Inst{19-16} = rd;
    let Inst{15-8} = dsp{7-0};
    let Inst{7-0} = dsp{15-8};
  }
  }

  // Stores: mov.sz Rs, [Rd] / Rs, dsp[Rd]
  let mayStore = 1 in {
  def _st0 : RXInst16<(outs), (ins GPR:$rs, GPR:$rd), mn#"\t$rs, [$rd]"> {
    bits<4> rs;
    bits<4> rd;
    let Inst{15-8} = {0b11, sz, 0b0011};
    let Inst{7-4} = rd;
    let Inst{3-0} = rs;
  }
  def _st5 : RXInst16<(outs), (ins GPRLow:$rs, GPRLow:$rd, dsp5:$dsp),
                      mn#"\t$rs, $dsp[$rd]"> {
    bits<3> rs;
    bits<3> rd;
    bits<5> dsp;
    let Inst{15-8} = {0b10, sz, 0b0, dsp{4-2}};
    let Inst{7} = dsp{1};
    let Inst{6-4} = rd;
    let Inst{3} = dsp{0};
    let Inst{2-0} = rs;
  }
  def _st8 : RXInst24<(outs), (ins GPR:$rs, GPR:$rd, dsp8:$dsp),
                      mn#"\t$rs, $dsp[$rd]"> {
    bits<4> rs;
    bits<4> rd;
    bits<8> dsp;
    let Inst{23-16} = {0b11, sz, 0b0111};
    let Inst{15-12} = rd;
    let Inst{11-8} = rs;
    let Inst{7-0} = dsp;
  }
  def _st16 : RXInst32<(outs), (ins GPR:$rs, GPR:$rd, dsp16:$dsp),
                       mn#"\t$rs, $dsp[$rd]"> {
    bits<4> rs;
    bits<4> rd;
    bits<16> dsp;
    let Inst{31-24} = {0b11, sz, 0b1011};
    let Inst{23-20} = rd;
    let Inst{19-16} = rs;
    let Inst{15-8} = dsp{7-0};
    let Inst{7-0} = dsp{15-8};
  }
  }
}

defm MOV_B : MovSized<"b", 0b00, dsp5b, dsp8b, dsp16b>;
defm MOV_W : MovSized<"w", 0b01, dsp5w, dsp8w, dsp16w>;
defm MOV_L : MovSized<"l", 0b10, dsp5l, dsp8l, dsp16l>;

// mov.l #imm, Rd
let isMoveImm = 1, isReMaterializable = 1, isAsCheapAsAMove = 1 in {
def MOV_L_ri4 : RXInst16<(outs GPR:$rd), (ins immu4:$imm),
                         "mov.l\t$imm, $rd"> {
  bits<4> rd;
  bits<4> imm;
  let Inst{15-8} = 0x66;
  let Inst{7-4} = imm;
  let Inst{3-0} = rd;
}
def MOV_L_ri8 : RXInst24<(outs GPR:$rd), (ins immu8:$imm),
                         "mov.l\t$imm, $rd"> {
  bits<4> rd;
  bits<8> imm;
  let Inst{23-16} = 0x75;
  let Inst{15-12} = 0x4;
  let Inst{11-8} = rd;
  let Inst{7-0} = imm;
}
// 1111 1011 rd[4] im[2] 10 imm...
def MOV_L_ris8 : RXInst24<(outs GPR:$rd), (ins imms8n:$imm),
                          "mov.l\t$imm, $rd"> {
  bits<4> rd;
  bits<8> imm;
  let Inst{23-16} = 0xfb;
  let Inst{15-12} = rd;
  let Inst{11-8} = 0b0110;
  let Inst{7-0} = imm;
}
def MOV_L_ri16 : RXInst32<(outs GPR:$rd), (ins imms16:$imm),
                          "mov.l\t$imm, $rd"> {
  bits<4> rd;
  bits<16> imm;
  let Inst{31-24} = 0xfb;
  let Inst{23-20} = rd;
  let Inst{19-16} = 0b1010;
  let Inst{15-8} = imm{7-0};
  let Inst{7-0} = imm{15-8};
}
def MOV_L_ri24 : RXInst40<(outs GPR:$rd), (ins imms24:$imm),
                          "mov.l\t$imm, $rd"> {
  bits<4> rd;
  bits<24> imm;
  let Inst{39-32} = 0xfb;
  let Inst{31-28} = rd;
  let Inst{27-24} = 0b1110;
  let Inst{23-16} = imm{7-0};
  let Inst{15-8} = imm{15-8};
  let Inst{7-0} = imm{23-16};
}
def MOV_L_ri32 : RXInst48<(outs GPR:$rd), (ins imm32:$imm),
                          "mov.l\t$imm, $rd"> {
  bits<4> rd;
  bits<32> imm;
  let Inst{47-40} = 0xfb;
  let Inst{39-36} = rd;
  let Inst{35-32} = 0b0010;
  let Inst{31-24} = imm{7-0};
  let Inst{23-16} = imm{15-8};
  let Inst{15-8} = imm{23-16};
  let Inst{7-0} = imm{31-24};
}
}

// movu.b / movu.w (zero-extending loads and register extension).
// s field: 0 = .B, 1 = .W
multiclass MovuSized<string sfx, bit s, Operand dsp5, Operand dsp8,
                     Operand dsp16> {
  defvar mn = "movu."#sfx;
  def _rr : RXInst16<(outs GPR:$rd), (ins GPR:$rs), mn#"\t$rs, $rd"> {
    bits<4> rs;
    bits<4> rd;
    let Inst{15-8} = {0b01011, s, 0b11};
    let Inst{7-4} = rs;
    let Inst{3-0} = rd;
  }
  let mayLoad = 1 in {
  def _ld0 : RXInst16<(outs GPR:$rd), (ins GPR:$rs), mn#"\t[$rs], $rd"> {
    bits<4> rs;
    bits<4> rd;
    let Inst{15-8} = {0b01011, s, 0b00};
    let Inst{7-4} = rs;
    let Inst{3-0} = rd;
  }
  def _ld5 : RXInst16<(outs GPRLow:$rd), (ins GPRLow:$rs, dsp5:$dsp),
                      mn#"\t$dsp[$rs], $rd"> {
    bits<3> rs;
    bits<3> rd;
    bits<5> dsp;
    let Inst{15-8} = {0b1011, s, dsp{4-2}};
    let Inst{7} = dsp{1};
    let Inst{6-4} = rs;
    let Inst{3} = dsp{0};
    let Inst{2-0} = rd;
  }
  def _ld8 : RXInst24<(outs GPR:$rd), (ins GPR:$rs, dsp8:$dsp),
                      mn#"\t$dsp[$rs], $rd"> {
    bits<4> rs;
    bits<4> rd;
    bits<8> dsp;
    let Inst{23-16} = {0b01011, s, 0b01};
    let Inst{15-12} = rs;
    let Inst{11-8} = rd;
    let Inst{7-0} = dsp;
  }
  def _ld16 : RXInst32<(outs GPR:$rd), (ins GPR:$rs, dsp16:$dsp),
                       mn#"\t$dsp[$rs], $rd"> {
    bits<4> rs;
    bits<4> rd;
    bits<16> dsp;
    let Inst{31-24} = {0b01011, s, 0b10};
    let Inst{23-20} = rs;
    let Inst{19-16} = rd;
    let Inst{15-8} = dsp{7-0};
    let Inst{7-0} = dsp{15-8};
  }
  }
}

defm MOVU_B : MovuSized<"b", 0, dsp5b, dsp8b, dsp16b>;
defm MOVU_W : MovuSized<"w", 1, dsp5w, dsp8w, dsp16w>;

//===----------------------------------------------------------------------===//
// Arithmetic / logic
//===----------------------------------------------------------------------===//

// Read-modify-write forms tie the destination to a $src input operand.

// 2-operand register forms: op Rs, Rd   (Rd = Rd op Rs)
class AluRR<bits<8> op, string mn>
    : RXInst16<(outs GPR:$rd), (ins GPR:$src, GPR:$rs), mn#"\t$rs, $rd"> {
  let Constraints = "$src = $rd";
  bits<4> rs;
  bits<4> rd;
  let Inst{15-8} = op;
  let Inst{7-4} = rs;
  let Inst{3-0} = rd;
}

// op #uimm4, Rd
class AluRI4<bits<8> op, string mn>
    : RXInst16<(outs GPR:$rd), (ins GPR:$src, immu4:$imm),
               mn#"\t$imm, $rd"> {
  let Constraints = "$src = $rd";
  bits<4> rd;
  bits<4> imm;
  let Inst{15-8} = op;
  let Inst{7-4} = imm;
  let Inst{3-0} = rd;
}

// 0111 01im sub[4] rd[4] imm  (mul / and / or)
multiclass Alu74<bits<4> sub, string mn> {
  let Constraints = "$src = $rd" in {
  def _ri8 : RXInst24<(outs GPR:$rd), (ins GPR:$src, imms8:$imm),
                      mn#"\t$imm, $rd"> {
    bits<4> rd;
    bits<8> imm;
    let Inst{23-16} = 0x75;
    let Inst{15-12} = sub;
    let Inst{11-8} = rd;
    let Inst{7-0} = imm;
  }
  def _ri16 : RXInst32<(outs GPR:$rd), (ins GPR:$src, imms16:$imm),
                       mn#"\t$imm, $rd"> {
    bits<4> rd;
    bits<16> imm;
    let Inst{31-24} = 0x76;
    let Inst{23-20} = sub;
    let Inst{19-16} = rd;
    let Inst{15-8} = imm{7-0};
    let Inst{7-0} = imm{15-8};
  }
  def _ri24 : RXInst40<(outs GPR:$rd), (ins GPR:$src, imms24:$imm),
                       mn#"\t$imm, $rd"> {
    bits<4> rd;
    bits<24> imm;
    let Inst{39-32} = 0x77;
    let Inst{31-28} = sub;
    let Inst{27-24} = rd;
    let Inst{23-16} = imm{7-0};
    let Inst{15-8} = imm{15-8};
    let Inst{7-0} = imm{23-16};
  }
  def _ri32 : RXInst48<(outs GPR:$rd), (ins GPR:$src, imm32:$imm),
                       mn#"\t$imm, $rd"> {
    bits<4> rd;
    bits<32> imm;
    let Inst{47-40} = 0x74;
    let Inst{39-36} = sub;
    let Inst{35-32} = rd;
    let Inst{31-24} = imm{7-0};
    let Inst{23-16} = imm{15-8};
    let Inst{15-8} = imm{23-16};
    let Inst{7-0} = imm{31-24};
  }
  }
}

// 1111 1100 op rs[4] rd[4]  (2-operand, Rd = Rd op Rs)
class AluFC<bits<8> op, string mn>
    : RXInst24<(outs GPR:$rd), (ins GPR:$src, GPR:$rs), mn#"\t$rs, $rd"> {
  let Constraints = "$src = $rd";
  bits<4> rs;
  bits<4> rd;
  let Inst{23-16} = 0xfc;
  let Inst{15-8} = op;
  let Inst{7-4} = rs;
  let Inst{3-0} = rd;
}

// 1111 1101 0111 im00 sub[4] rd[4] imm
multiclass AluFD70<bits<4> sub, string mn> {
  let Constraints = "$src = $rd" in {
  def _ri8 : RXInst32<(outs GPR:$rd), (ins GPR:$src, imms8:$imm),
                      mn#"\t$imm, $rd"> {
    bits<4> rd;
    bits<8> imm;
    let Inst{31-24} = 0xfd;
    let Inst{23-16} = 0x74;
    let Inst{15-12} = sub;
    let Inst{11-8} = rd;
    let Inst{7-0} = imm;
  }
  def _ri16 : RXInst40<(outs GPR:$rd), (ins GPR:$src, imms16:$imm),
                       mn#"\t$imm, $rd"> {
    bits<4> rd;
    bits<16> imm;
    let Inst{39-32} = 0xfd;
    let Inst{31-24} = 0x78;
    let Inst{23-20} = sub;
    let Inst{19-16} = rd;
    let Inst{15-8} = imm{7-0};
    let Inst{7-0} = imm{15-8};
  }
  def _ri24 : RXInst48<(outs GPR:$rd), (ins GPR:$src, imms24:$imm),
                       mn#"\t$imm, $rd"> {
    bits<4> rd;
    bits<24> imm;
    let Inst{47-40} = 0xfd;
    let Inst{39-32} = 0x7c;
    let Inst{31-28} = sub;
    let Inst{27-24} = rd;
    let Inst{23-16} = imm{7-0};
    let Inst{15-8} = imm{15-8};
    let Inst{7-0} = imm{23-16};
  }
  def _ri32 : RXInst56<(outs GPR:$rd), (ins GPR:$src, imm32:$imm),
                       mn#"\t$imm, $rd"> {
    bits<4> rd;
    bits<32> imm;
    let Inst{55-48} = 0xfd;
    let Inst{47-40} = 0x70;
    let Inst{39-36} = sub;
    let Inst{35-32} = rd;
    let Inst{31-24} = imm{7-0};
    let Inst{23-16} = imm{15-8};
    let Inst{15-8} = imm{23-16};
    let Inst{7-0} = imm{31-24};
  }
  }
}

// Single-register forms: 0111 1110 sub[4] rd[4]
class AluUnary<bits<4> sub, string mn>
    : RXInst16<(outs GPR:$rd), (ins GPR:$src), mn#"\t$rd"> {
  let Constraints = "$src = $rd";
  bits<4> rd;
  let Inst{15-8} = 0x7e;
  let Inst{7-4} = sub;
  let Inst{3-0} = rd;
}

// Shifts by an immediate: 0110 1sub(2) i | imm[3:0] rd
class ShiftRI<bits<2> sub, string mn>
    : RXInst16<(outs GPR:$rd), (ins GPR:$src, immu5:$imm),
               mn#"\t$imm, $rd"> {
  let Constraints = "$src = $rd";
  bits<4> rd;
  bits<5> imm;
  let Inst{15-8} = {0b0110, 0b1, sub, imm{4}};
  let Inst{7-4} = imm{3-0};
  let Inst{3-0} = rd;
}

// Register-count shifts and rotates: 1111 1101 op rs rd
class AluFDRR<bits<8> op, string mn>
    : RXInst24<(outs GPR:$rd), (ins GPR:$src, GPR:$rs), mn#"\t$rs, $rd"> {
  let Constraints = "$src = $rd";
  bits<4> rs;
  bits<4> rd;
  let Inst{23-16} = 0xfd;
  let Inst{15-8} = op;
  let Inst{7-4} = rs;
  let Inst{3-0} = rd;
}

// Rotates by an immediate: 1111 1101 0110 11xi imm[3:0] rd
class RotRI<bit l, string mn>
    : RXInst24<(outs GPR:$rd), (ins GPR:$src, immu5:$imm),
               mn#"\t$imm, $rd"> {
  let Constraints = "$src = $rd";
  bits<4> rd;
  bits<5> imm;
  let Inst{23-16} = 0xfd;
  let Inst{15-8} = {0b011011, l, imm{4}};
  let Inst{7-4} = imm{3-0};
  let Inst{3-0} = rd;
}

// Arithmetic, logic and shift instructions update the flags in PSW.
let Defs = [PSW] in {

// ADD: #uimm4 / 3-operand immediate / 2- and 3-operand register.
def ADD_ri4 : AluRI4<0x62, "add">;
def ADD_rr  : AluRR<0x4b, "add">;

// add #imm, Rs, Rd: 0111 00im rs rd imm
multiclass Add3Imm {
  def _rri8 : RXInst24<(outs GPR:$rd), (ins GPR:$rs, imms8:$imm),
                       "add\t$imm, $rs, $rd"> {
    bits<4> rs;
    bits<4> rd;
    bits<8> imm;
    let Inst{23-16} = 0x71;
    let Inst{15-12} = rs;
    let Inst{11-8} = rd;
    let Inst{7-0} = imm;
  }
  def _rri16 : RXInst32<(outs GPR:$rd), (ins GPR:$rs, imms16:$imm),
                        "add\t$imm, $rs, $rd"> {
    bits<4> rs;
    bits<4> rd;
    bits<16> imm;
    let Inst{31-24} = 0x72;
    let Inst{23-20} = rs;
    let Inst{19-16} = rd;
    let Inst{15-8} = imm{7-0};
    let Inst{7-0} = imm{15-8};
  }
  def _rri24 : RXInst40<(outs GPR:$rd), (ins GPR:$rs, imms24:$imm),
                        "add\t$imm, $rs, $rd"> {
    bits<4> rs;
    bits<4> rd;
    bits<24> imm;
    let Inst{39-32} = 0x73;
    let Inst{31-28} = rs;
    let Inst{27-24} = rd;
    let Inst{23-16} = imm{7-0};
    let Inst{15-8} = imm{15-8};
    let Inst{7-0} = imm{23-16};
  }
  def _rri32 : RXInst48<(outs GPR:$rd), (ins GPR:$rs, imm32:$imm),
                        "add\t$imm, $rs, $rd"> {
    bits<4> rs;
    bits<4> rd;
    bits<32> imm;
    let Inst{47-40} = 0x70;
    let Inst{39-36} = rs;
    let Inst{35-32} = rd;
    let Inst{31-24} = imm{7-0};
    let Inst{23-16} = imm{15-8};
    let Inst{15-8} = imm{23-16};
    let Inst{7-0} = imm{31-24};
  }
}
defm ADD : Add3Imm;

} // Defs = [PSW]

// "add #imm, Rd" with a non-uimm4 immediate is "add #imm, Rd, Rd".
def : InstAlias<"add\t$imm, $rd", (ADD_rri8 GPR:$rd, GPR:$rd, imms8:$imm)>;
def : InstAlias<"add\t$imm, $rd", (ADD_rri16 GPR:$rd, GPR:$rd, imms16:$imm)>;
def : InstAlias<"add\t$imm, $rd", (ADD_rri24 GPR:$rd, GPR:$rd, imms24:$imm)>;
def : InstAlias<"add\t$imm, $rd", (ADD_rri32 GPR:$rd, GPR:$rd, imm32:$imm)>;

let Defs = [PSW] in {

// "sub #imm, Rd" with a non-uimm4 immediate is assembled as an ADD of the
// negated value (done in the AsmParser, as GNU as does).
def SUB_ri4 : AluRI4<0x60, "sub">;
def SUB_rr  : AluRR<0x43, "sub">;

def MUL_ri4 : AluRI4<0x63, "mul">;
def MUL_rr  : AluRR<0x4f, "mul">;
defm MUL    : Alu74<0x1, "mul">;

def AND_ri4 : AluRI4<0x64, "and">;
def AND_rr  : AluRR<0x53, "and">;
defm AND    : Alu74<0x2, "and">;

def OR_ri4  : AluRI4<0x65, "or">;
def OR_rr   : AluRR<0x57, "or">;
defm OR     : Alu74<0x3, "or">;

// 3-operand register forms: 1111 1111 op[4] rd[4] rs[4] rs2[4]
class Alu3RR<bits<4> op, string mn>
    : RXInst24<(outs GPR:$rd), (ins GPR:$rs, GPR:$rs2),
               mn#"\t$rs, $rs2, $rd"> {
  bits<4> rs;
  bits<4> rs2;
  bits<4> rd;
  let Inst{23-16} = 0xff;
  let Inst{15-12} = op;
  let Inst{11-8} = rd;
  let Inst{7-4} = rs;
  let Inst{3-0} = rs2;
}

def SUB_rrr : Alu3RR<0x0, "sub">;
def ADD_rrr : Alu3RR<0x2, "add">;
def MUL_rrr : Alu3RR<0x3, "mul">;
def AND_rrr : Alu3RR<0x4, "and">;
def OR_rrr  : Alu3RR<0x5, "or">;
let Predicates = [HasRXv3] in
def XOR_rrr : Alu3RR<0x6, "xor">;

// CMP: no destination register.
let Defs = [PSW] in {
def CMP_ri4 : RXInst16<(outs), (ins GPR:$rd, immu4:$imm), "cmp\t$imm, $rd"> {
  bits<4> rd;
  bits<4> imm;
  let Inst{15-8} = 0x61;
  let Inst{7-4} = imm;
  let Inst{3-0} = rd;
}
def CMP_ri8 : RXInst24<(outs), (ins GPR:$rd, immu8:$imm), "cmp\t$imm, $rd"> {
  bits<4> rd;
  bits<8> imm;
  let Inst{23-16} = 0x75;
  let Inst{15-12} = 0x5;
  let Inst{11-8} = rd;
  let Inst{7-0} = imm;
}
def CMP_ris8 : RXInst24<(outs), (ins GPR:$rd, imms8n:$imm),
                        "cmp\t$imm, $rd"> {
  bits<4> rd;
  bits<8> imm;
  let Inst{23-16} = 0x75;
  let Inst{15-12} = 0x0;
  let Inst{11-8} = rd;
  let Inst{7-0} = imm;
}
def CMP_ri16 : RXInst32<(outs), (ins GPR:$rd, imms16:$imm),
                        "cmp\t$imm, $rd"> {
  bits<4> rd;
  bits<16> imm;
  let Inst{31-24} = 0x76;
  let Inst{23-20} = 0x0;
  let Inst{19-16} = rd;
  let Inst{15-8} = imm{7-0};
  let Inst{7-0} = imm{15-8};
}
def CMP_ri24 : RXInst40<(outs), (ins GPR:$rd, imms24:$imm),
                        "cmp\t$imm, $rd"> {
  bits<4> rd;
  bits<24> imm;
  let Inst{39-32} = 0x77;
  let Inst{31-28} = 0x0;
  let Inst{27-24} = rd;
  let Inst{23-16} = imm{7-0};
  let Inst{15-8} = imm{15-8};
  let Inst{7-0} = imm{23-16};
}
def CMP_ri32 : RXInst48<(outs), (ins GPR:$rd, imm32:$imm),
                        "cmp\t$imm, $rd"> {
  bits<4> rd;
  bits<32> imm;
  let Inst{47-40} = 0x74;
  let Inst{39-36} = 0x0;
  let Inst{35-32} = rd;
  let Inst{31-24} = imm{7-0};
  let Inst{23-16} = imm{15-8};
  let Inst{15-8} = imm{23-16};
  let Inst{7-0} = imm{31-24};
}
def CMP_rr : RXInst16<(outs), (ins GPR:$rd, GPR:$rs), "cmp\t$rs, $rd"> {
  bits<4> rs;
  bits<4> rd;
  let Inst{15-8} = 0x47;
  let Inst{7-4} = rs;
  let Inst{3-0} = rd;
}
}

// FC-prefixed register-register operations. ADC and SBB read the carry.
let Uses = [PSW] in {
def SBB_rr   : AluFC<0x03, "sbb">;
def ADC_rr   : AluFC<0x0b, "adc">;
}
def NEG_rr   : AluFC<0x07, "neg">;
def ABS_rr   : AluFC<0x0f, "abs">;
def MAX_rr   : AluFC<0x13, "max">;
def MIN_rr   : AluFC<0x17, "min">;
def EMUL_rr  : AluFC<0x1b, "emul">;
def EMULU_rr : AluFC<0x1f, "emulu">;
def DIV_rr   : AluFC<0x23, "div">;
def DIVU_rr  : AluFC<0x27, "divu">;
def XOR_rr   : AluFC<0x37, "xor">;
def NOT_rr   : AluFC<0x3b, "not">;
let Defs = [PSW] in
def TST_rr : RXInst24<(outs), (ins GPR:$rd, GPR:$rs), "tst\t$rs, $rd"> {
  bits<4> rs;
  bits<4> rd;
  let Inst{23-16} = 0xfc;
  let Inst{15-8} = 0x33;
  let Inst{7-4} = rs;
  let Inst{3-0} = rd;
}

// FD 70-prefixed immediate operations.
let Uses = [PSW] in
defm ADC   : AluFD70<0x2, "adc">;
defm MAX   : AluFD70<0x4, "max">;
defm MIN   : AluFD70<0x5, "min">;
defm EMUL  : AluFD70<0x6, "emul">;
defm EMULU : AluFD70<0x7, "emulu">;
defm DIV   : AluFD70<0x8, "div">;
defm DIVU  : AluFD70<0x9, "divu">;
defm XOR   : AluFD70<0xd, "xor">;

def NOT_r  : AluUnary<0x0, "not">;
def NEG_r  : AluUnary<0x1, "neg">;
def ABS_r  : AluUnary<0x2, "abs">;
def SAT_r  : AluUnary<0x3, "sat">;
def RORC_r : AluUnary<0x4, "rorc">;
def ROLC_r : AluUnary<0x5, "rolc">;

// Shifts.
def SHLR_ri : ShiftRI<0b00, "shlr">;
def SHAR_ri : ShiftRI<0b01, "shar">;
def SHLL_ri : ShiftRI<0b10, "shll">;
def SHLR_rr : AluFDRR<0x60, "shlr">;
def SHAR_rr : AluFDRR<0x61, "shar">;
def SHLL_rr : AluFDRR<0x62, "shll">;

// shift #imm, Rs, Rd: 1111 1101 1sub(2)imm[5] rs rd
class Shift3RI<bits<2> sub, string mn>
    : RXInst24<(outs GPR:$rd), (ins immu5:$imm, GPR:$rs),
               mn#"\t$imm, $rs, $rd"> {
  bits<4> rs;
  bits<4> rd;
  bits<5> imm;
  let Inst{23-16} = 0xfd;
  let Inst{15-8} = {0b1, sub, imm};
  let Inst{7-4} = rs;
  let Inst{3-0} = rd;
}
def SHLR_rri : Shift3RI<0b00, "shlr">;
def SHAR_rri : Shift3RI<0b01, "shar">;
def SHLL_rri : Shift3RI<0b10, "shll">;

def ROTR_rr : AluFDRR<0x64, "rotr">;
def ROTL_rr : AluFDRR<0x66, "rotl">;
def ROTR_ri : RotRI<0, "rotr">;
def ROTL_ri : RotRI<1, "rotl">;

// revw / revl: not read-modify-write.
class AluFDRRnt<bits<8> op, string mn>
    : RXInst24<(outs GPR:$rd), (ins GPR:$rs), mn#"\t$rs, $rd"> {
  bits<4> rs;
  bits<4> rd;
  let Inst{23-16} = 0xfd;
  let Inst{15-8} = op;
  let Inst{7-4} = rs;
  let Inst{3-0} = rd;
}
} // Defs = [PSW]

def REVW_rr : AluFDRRnt<0x65, "revw">;
def REVL_rr : AluFDRRnt<0x67, "revl">;

//===----------------------------------------------------------------------===//
// Stack
//===----------------------------------------------------------------------===//

let Uses = [R0], Defs = [R0] in {
let mayStore = 1 in {
// push.sz Rs: 0111 1110 10 sz rs
class PushR<bits<2> sz, string sfx>
    : RXInst16<(outs), (ins GPR:$rs), "push."#sfx#"\t$rs"> {
  bits<4> rs;
  let Inst{15-8} = 0x7e;
  let Inst{7-4} = {0b10, sz};
  let Inst{3-0} = rs;
}
def PUSH_B : PushR<0b00, "b">;
def PUSH_W : PushR<0b01, "w">;
def PUSH_L : PushR<0b10, "l">;

def PUSHM : RXInst16<(outs), (ins GPR:$lo, GPR:$hi), "pushm\t$lo-$hi"> {
  bits<4> lo;
  bits<4> hi;
  let Inst{15-8} = 0x6e;
  let Inst{7-4} = lo;
  let Inst{3-0} = hi;
}
}

let mayLoad = 1 in {
def POP : RXInst16<(outs GPR:$rd), (ins), "pop\t$rd"> {
  bits<4> rd;
  let Inst{15-8} = 0x7e;
  let Inst{7-4} = 0xb;
  let Inst{3-0} = rd;
}
def POPM : RXInst16<(outs), (ins GPR:$lo, GPR:$hi), "popm\t$lo-$hi"> {
  bits<4> lo;
  bits<4> hi;
  let Inst{15-8} = 0x6f;
  let Inst{7-4} = lo;
  let Inst{3-0} = hi;
}
}

let isReturn = 1, isTerminator = 1, isBarrier = 1, mayLoad = 1 in {
def RTSD_i : RXInst16<(outs), (ins rtsdimm:$imm), "rtsd\t$imm"> {
  bits<8> imm;
  let Inst{15-8} = 0x67;
  let Inst{7-0} = imm;
}
def RTSD_irr : RXInst24<(outs), (ins rtsdimm:$imm, GPR:$lo, GPR:$hi),
                        "rtsd\t$imm, $lo-$hi"> {
  bits<8> imm;
  bits<4> lo;
  bits<4> hi;
  let Inst{23-16} = 0x3f;
  let Inst{15-12} = lo;
  let Inst{11-8} = hi;
  let Inst{7-0} = imm;
}
}
} // Uses = [R0], Defs = [R0]

//===----------------------------------------------------------------------===//
// Branches, calls and returns
//===----------------------------------------------------------------------===//

class RXInst1<bits<8> op, string asmstr>
    : RXInst8<(outs), (ins), asmstr> {
  let Inst = op;
}

class RXInst2<bits<16> op, string asmstr>
    : RXInst16<(outs), (ins), asmstr> {
  let Inst = op;
}

def BRK : RXInst1<0x00, "brk">;
def DBT : RXInst1<0x01, "dbt">;

let isReturn = 1, isTerminator = 1, isBarrier = 1 in
def RTS : RXInst1<0x02, "rts">;

def NOP : RXInst1<0x03, "nop">;

//===----------------------------------------------------------------------===//
// System control: control registers, PSW flags, interrupt priority
//===----------------------------------------------------------------------===//

// PSW flag operand of setpsw / clrpsw.
class RXFlag<string n, int v> {
  string Name = n;
  int Value = v;
}

let hasSideEffects = 1 in {
// mvtc Rs, cr: 1111 1101 0110 100c rs cr   (c = cr bit 4; all cr here < 16)
def MVTC_r : RXInst24<(outs CTRL:$cr), (ins GPR:$rs), "mvtc\t$rs, $cr"> {
  bits<4> rs;
  bits<4> cr;
  let Inst{23-16} = 0xfd;
  let Inst{15-8} = 0x68;
  let Inst{7-4} = rs;
  let Inst{3-0} = cr;
}
// mvfc cr, Rd: 1111 1101 0110 101c cr rd
def MVFC : RXInst24<(outs GPR:$rd), (ins CTRL:$cr), "mvfc\t$cr, $rd"> {
  bits<4> rd;
  bits<4> cr;
  let Inst{23-16} = 0xfd;
  let Inst{15-8} = 0x6a;
  let Inst{7-4} = cr;
  let Inst{3-0} = rd;
}
// mvtc #imm, cr: 1111 1101 0111 im11 000 cr[5] imm
def MVTC_i8 : RXInst32<(outs CTRL:$cr), (ins imms8:$imm), "mvtc\t$imm, $cr"> {
  bits<4> cr;
  bits<8> imm;
  let Inst{31-24} = 0xfd;
  let Inst{23-16} = 0x77;
  let Inst{15-8} = {0b0000, cr};
  let Inst{7-0} = imm;
}
def MVTC_i16 : RXInst40<(outs CTRL:$cr), (ins imms16:$imm),
                        "mvtc\t$imm, $cr"> {
  bits<4> cr;
  bits<16> imm;
  let Inst{39-32} = 0xfd;
  let Inst{31-24} = 0x7b;
  let Inst{23-16} = {0b0000, cr};
  let Inst{15-8} = imm{7-0};
  let Inst{7-0} = imm{15-8};
}
def MVTC_i24 : RXInst48<(outs CTRL:$cr), (ins imms24:$imm),
                        "mvtc\t$imm, $cr"> {
  bits<4> cr;
  bits<24> imm;
  let Inst{47-40} = 0xfd;
  let Inst{39-32} = 0x7f;
  let Inst{31-24} = {0b0000, cr};
  let Inst{23-16} = imm{7-0};
  let Inst{15-8} = imm{15-8};
  let Inst{7-0} = imm{23-16};
}
def MVTC_i32 : RXInst56<(outs CTRL:$cr), (ins imm32:$imm),
                        "mvtc\t$imm, $cr"> {
  bits<4> cr;
  bits<32> imm;
  let Inst{55-48} = 0xfd;
  let Inst{47-40} = 0x73;
  let Inst{39-32} = {0b0000, cr};
  let Inst{31-24} = imm{7-0};
  let Inst{23-16} = imm{15-8};
  let Inst{15-8} = imm{23-16};
  let Inst{7-0} = imm{31-24};
}

let Uses = [R0], Defs = [R0] in {
// pushc cr: 0111 1110 110 cr / popc cr: 0111 1110 111 cr
def PUSHC : RXInst16<(outs), (ins CTRL:$cr), "pushc\t$cr"> {
  bits<4> cr;
  let Inst{15-8} = 0x7e;
  let Inst{7-4} = 0xc;
  let Inst{3-0} = cr;
}
def POPC : RXInst16<(outs CTRL:$cr), (ins), "popc\t$cr"> {
  bits<4> cr;
  let Inst{15-8} = 0x7e;
  let Inst{7-4} = 0xe;
  let Inst{3-0} = cr;
}
}

// mvtipl #imm: 0111 0101 0111 0000 0000 imm[4]
def MVTIPL : RXInst24<(outs), (ins immu4:$imm), "mvtipl\t$imm"> {
  bits<4> imm;
  let Inst{23-8} = 0x7570;
  let Inst{7-4} = 0x0;
  let Inst{3-0} = imm;
}
// int #imm: 0111 0101 0110 0000 imm[8]
def INT : RXInst24<(outs), (ins immu8:$imm), "int\t$imm"> {
  bits<8> imm;
  let Inst{23-8} = 0x7560;
  let Inst{7-0} = imm;
}

// setpsw / clrpsw <flag>: 0111 1111 1010 flag / 0111 1111 1011 flag
foreach F = [RXFlag<"c", 0>, RXFlag<"z", 1>, RXFlag<"s", 2>, RXFlag<"o", 3>,
             RXFlag<"i", 8>, RXFlag<"u", 9>] in {
  def SETPSW_#!toupper(F.Name) : RXInst2<!add(0x7fa0, F.Value),
                                         "setpsw\t"#F.Name>;
  def CLRPSW_#!toupper(F.Name) : RXInst2<!add(0x7fb0, F.Value),
                                         "clrpsw\t"#F.Name>;
}
}

def SATR : RXInst2<0x7f93, "satr">;
def RTFI : RXInst2<0x7f94, "rtfi">;
def RTE  : RXInst2<0x7f95, "rte">;
def WAIT : RXInst2<0x7f96, "wait">;

// Register-indirect jumps and calls: 0111 1111 op rs
class BranchR<bits<4> op, string mn>
    : RXInst16<(outs), (ins GPR:$rs), mn#"\t$rs"> {
  bits<4> rs;
  let Inst{15-8} = 0x7f;
  let Inst{7-4} = op;
  let Inst{3-0} = rs;
}

let isBranch = 1, isTerminator = 1, isBarrier = 1, isIndirectBranch = 1 in {
def JMP   : BranchR<0x0, "jmp">;
def BRA_L : BranchR<0x4, "bra.l">;
}
let isCall = 1, Uses = [R0], Defs = [R0] in {
def JSR   : BranchR<0x1, "jsr">;
def BSR_L : BranchR<0x5, "bsr.l">;
}

// PC-relative branches. The displacement is relative to the first byte of
// the instruction.
class Br8<bits<8> op, string mn>
    : RXInst16<(outs), (ins brtarget8:$dst), mn#"\t$dst"> {
  bits<8> dst;
  let Inst{15-8} = op;
  let Inst{7-0} = dst;
}
class Br16<bits<8> op, string mn>
    : RXInst24<(outs), (ins brtarget16:$dst), mn#"\t$dst"> {
  bits<16> dst;
  let Inst{23-16} = op;
  let Inst{15-8} = dst{7-0};
  let Inst{7-0} = dst{15-8};
}
class Br24<bits<8> op, string mn>
    : RXInst32<(outs), (ins brtarget24:$dst), mn#"\t$dst"> {
  bits<24> dst;
  let Inst{31-24} = op;
  let Inst{23-16} = dst{7-0};
  let Inst{15-8} = dst{15-8};
  let Inst{7-0} = dst{23-16};
}
// bra.s / beq.s / bne.s: 0000 1ddd, 0001 0ddd, 0001 1ddd (dsp 3..10)
class BrS<bits<5> op, string mn>
    : RXInst8<(outs), (ins brtarget3:$dst), mn#"\t$dst"> {
  bits<3> dst;
  let Inst{7-3} = op;
  let Inst{2-0} = dst;
}

// Condition codes of BCnd (the 4-bit cond field).
class RXCC<string n, int v> {
  string Name = n;
  int Value = v;
}
def CC_EQ  : RXCC<"eq",  0x0>;
def CC_NE  : RXCC<"ne",  0x1>;
def CC_GEU : RXCC<"geu", 0x2>;
def CC_LTU : RXCC<"ltu", 0x3>;
def CC_GTU : RXCC<"gtu", 0x4>;
def CC_LEU : RXCC<"leu", 0x5>;
def CC_PZ  : RXCC<"pz",  0x6>;
def CC_N   : RXCC<"n",   0x7>;
def CC_GE  : RXCC<"ge",  0x8>;
def CC_LT  : RXCC<"lt",  0x9>;
def CC_GT  : RXCC<"gt",  0xa>;
def CC_LE  : RXCC<"le",  0xb>;
def CC_O   : RXCC<"o",   0xc>;
def CC_NO  : RXCC<"no",  0xd>;

let isBranch = 1, isTerminator = 1, isBarrier = 1 in {
def BRA_S : BrS<0b00001, "bra.s">;
def BRA_B : Br8<0x2e, "bra.b">;
def BRA_W : Br16<0x38, "bra.w">;
def BRA_A : Br24<0x04, "bra.a">;
}

let isCall = 1, Uses = [R0], Defs = [R0] in {
def BSR_W : Br16<0x39, "bsr.w">;
def BSR_A : Br24<0x05, "bsr.a">;
}

let isBranch = 1, isTerminator = 1, Uses = [PSW] in {
def BEQ_S : BrS<0b00010, "beq.s">;
def BNE_S : BrS<0b00011, "bne.s">;
def BEQ_W : Br16<0x3a, "beq.w">;
def BNE_W : Br16<0x3b, "bne.w">;

// b<cond>.b: 0010 cond disp8
foreach C = [CC_EQ, CC_NE, CC_GEU, CC_LTU, CC_GTU, CC_LEU, CC_PZ, CC_N,
             CC_GE, CC_LT, CC_GT, CC_LE, CC_O, CC_NO] in
  def B#!toupper(C.Name)#_B : Br8<!add(0x20, C.Value), "b"#C.Name#".b">;
}

// Alternative condition names accepted by GNU as.
def : InstAlias<"bz.b\t$dst",  (BEQ_B brtarget8:$dst), 0>;
def : InstAlias<"bnz.b\t$dst", (BNE_B brtarget8:$dst), 0>;
def : InstAlias<"bc.b\t$dst",  (BGEU_B brtarget8:$dst), 0>;
def : InstAlias<"bnc.b\t$dst", (BLTU_B brtarget8:$dst), 0>;
def : InstAlias<"bz.s\t$dst",  (BEQ_S brtarget3:$dst), 0>;
def : InstAlias<"bnz.s\t$dst", (BNE_S brtarget3:$dst), 0>;
def : InstAlias<"bz.w\t$dst",  (BEQ_W brtarget16:$dst), 0>;
def : InstAlias<"bnz.w\t$dst", (BNE_W brtarget16:$dst), 0>;

// sc<cond>.l Rd: Rd = cond ? 1 : 0   (1111 1100 1101 1011 rd cond)
let Uses = [PSW] in
foreach C = [CC_EQ, CC_NE, CC_GEU, CC_LTU, CC_GTU, CC_LEU, CC_PZ, CC_N,
             CC_GE, CC_LT, CC_GT, CC_LE, CC_O, CC_NO] in
  def SC#!toupper(C.Name)#_L
      : RXInst24<(outs GPR:$rd), (ins), "sc"#C.Name#".l\t$rd"> {
    bits<4> rd;
    let Inst{23-16} = 0xfc;
    let Inst{15-8} = 0xdb;
    let Inst{7-4} = rd;
    let Inst{3-0} = C.Value;
  }

//===----------------------------------------------------------------------===//
// Code generation
//
// Instruction selection always picks the longest form of an instruction
// (32-bit immediate, 16-bit displacement, 3-operand); RXMCInstLower then
// shrinks each instruction to the shortest encoding for its final operands,
// using the same rules as the assembler.
//===----------------------------------------------------------------------===//

def SDT_RXCall         : SDTypeProfile<0, -1, [SDTCisVT<0, iPTR>]>;
def SDT_RXCallSeqStart : SDCallSeqStart<[SDTCisVT<0, i32>, SDTCisVT<1, i32>]>;
def SDT_RXCallSeqEnd   : SDCallSeqEnd<[SDTCisVT<0, i32>, SDTCisVT<1, i32>]>;
def SDT_RXWrapper      : SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>,
                                              SDTCisPtrTy<0>]>;
def SDT_RXCmp          : SDTypeProfile<0, 2, [SDTCisSameAs<0, 1>]>;
def SDT_RXBrCC         : SDTypeProfile<0, 2, [SDTCisVT<0, OtherVT>,
                                              SDTCisVT<1, i32>]>;
def SDT_RXSelectCC     : SDTypeProfile<1, 3, [SDTCisSameAs<0, 1>,
                                              SDTCisSameAs<1, 2>,
                                              SDTCisVT<3, i32>]>;
def SDT_RXSetCC        : SDTypeProfile<1, 1, [SDTCisVT<0, i32>,
                                              SDTCisVT<1, i32>]>;

def RXretglue : SDNode<"RXISD::RET_GLUE", SDTNone,
                       [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
def RXcall    : SDNode<"RXISD::CALL", SDT_RXCall,
                       [SDNPHasChain, SDNPOutGlue, SDNPOptInGlue,
                        SDNPVariadic]>;
def RXcallseq_start : SDNode<"ISD::CALLSEQ_START", SDT_RXCallSeqStart,
                             [SDNPHasChain, SDNPOutGlue]>;
def RXcallseq_end   : SDNode<"ISD::CALLSEQ_END", SDT_RXCallSeqEnd,
                             [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;
def RXWrapper  : SDNode<"RXISD::Wrapper", SDT_RXWrapper>;
def RXcmp      : SDNode<"RXISD::CMP", SDT_RXCmp, [SDNPOutGlue]>;
def RXbrcc     : SDNode<"RXISD::BR_CC", SDT_RXBrCC, [SDNPHasChain, SDNPInGlue]>;
def RXselectcc : SDNode<"RXISD::SELECT_CC", SDT_RXSelectCC, [SDNPInGlue]>;
def RXsetcc    : SDNode<"RXISD::SETCC", SDT_RXSetCC, [SDNPInGlue]>;

// Base register (or frame index) + displacement for an access of the given
// size; the displacement is a multiple of the size and fits dsp:16.
def AddrB : ComplexPattern<iPTR, 2, "SelectAddrB", [frameindex], []>;
def AddrW : ComplexPattern<iPTR, 2, "SelectAddrW", [frameindex], []>;
def AddrL : ComplexPattern<iPTR, 2, "SelectAddrL", [frameindex], []>;

def uimm5 : ImmLeaf<i32, [{ return isUInt<5>(Imm); }]>;

class RXPseudo<dag outs, dag ins, list<dag> pattern = []>
    : RXInst<0, outs, ins, "", pattern> {
  let isPseudo = 1;
  let isCodeGenOnly = 1;
}

let Defs = [R0, PSW], Uses = [R0] in {
def ADJCALLSTACKDOWN : RXPseudo<(outs), (ins i32imm:$amt1, i32imm:$amt2),
                                [(RXcallseq_start timm:$amt1, timm:$amt2)]>;
def ADJCALLSTACKUP   : RXPseudo<(outs), (ins i32imm:$amt1, i32imm:$amt2),
                                [(RXcallseq_end timm:$amt1, timm:$amt2)]>;
}

// Address of a stack object; becomes "add #offset, r0, rd".
let Defs = [PSW] in
def FRMIDX : RXPseudo<(outs GPR:$rd), (ins i32imm:$fi, i32imm:$off)>;

let usesCustomInserter = 1, Uses = [PSW] in
def SELECT : RXPseudo<(outs GPR:$rd), (ins GPR:$t, GPR:$f, i32imm:$cc),
                      [(set GPR:$rd, (RXselectcc GPR:$t, GPR:$f, imm:$cc))]>;

// Conditional branch on a condition code; lowered to b<cond>.b. Branch
// relaxation inverts it around a bra.a when the target is out of range.
let isBranch = 1, isTerminator = 1, Uses = [PSW], Size = 2 in
def BCC : RXPseudo<(outs), (ins brtarget8:$dst, i32imm:$cc),
                   [(RXbrcc bb:$dst, imm:$cc)]>;

// sc<cond>.l with the condition as an operand.
let Uses = [PSW], Size = 3 in
def SCC : RXPseudo<(outs GPR:$rd), (ins i32imm:$cc),
                   [(set GPR:$rd, (RXsetcc imm:$cc))]>;

// Direct call (bsr.a to a symbol).
def calltarget : Operand<i32> {
  let EncoderMethod = "getBrTarget24OpValue";
  let PrintMethod = "printPCRel";
}
let isCall = 1, isCodeGenOnly = 1, Uses = [R0], Defs = [R0] in
def BSR_call : RXInst32<(outs), (ins calltarget:$dst), "bsr.a\t$dst"> {
  bits<24> dst;
  let Inst{31-24} = 0x05;
  let Inst{23-16} = dst{7-0};
  let Inst{15-8} = dst{15-8};
  let Inst{7-0} = dst{23-16};
}

def : Pat<(RXcall tglobaladdr:$dst), (BSR_call tglobaladdr:$dst)>;
def : Pat<(RXcall texternalsym:$dst), (BSR_call texternalsym:$dst)>;
def : Pat<(RXcall GPR:$rs), (JSR GPR:$rs)>;
def : Pat<(RXretglue), (RTS)>;
def : Pat<(br bb:$dst), (BRA_A bb:$dst)>;

// Constants and addresses.
def : Pat<(i32 imm:$imm), (MOV_L_ri32 imm:$imm)>;
def : Pat<(RXWrapper tglobaladdr:$a), (MOV_L_ri32 tglobaladdr:$a)>;
def : Pat<(RXWrapper texternalsym:$a), (MOV_L_ri32 texternalsym:$a)>;
def : Pat<(RXWrapper tblockaddress:$a), (MOV_L_ri32 tblockaddress:$a)>;

// Loads and stores. The pseudos take a (base, dsp) memory operand so that
// the AddrX complex patterns can fill it; RXMCInstLower turns them into the
// MOV/MOVU dsp:16 forms, whose operands are laid out the same way.
// MOV.B/MOV.W loads sign-extend, MOVU zero-extends.
def mem : Operand<i32> {
  let MIOperandInfo = (ops GPR, i32imm);
}

let Size = 4 in {
let mayLoad = 1 in {
def LD_L  : RXPseudo<(outs GPR:$rd), (ins mem:$a),
                     [(set GPR:$rd, (i32 (load AddrL:$a)))]>;
def LD_B  : RXPseudo<(outs GPR:$rd), (ins mem:$a),
                     [(set GPR:$rd, (i32 (sextloadi8 AddrB:$a)))]>;
def LD_W  : RXPseudo<(outs GPR:$rd), (ins mem:$a),
                     [(set GPR:$rd, (i32 (sextloadi16 AddrW:$a)))]>;
def LD_UB : RXPseudo<(outs GPR:$rd), (ins mem:$a),
                     [(set GPR:$rd, (i32 (zextloadi8 AddrB:$a)))]>;
def LD_UW : RXPseudo<(outs GPR:$rd), (ins mem:$a),
                     [(set GPR:$rd, (i32 (zextloadi16 AddrW:$a)))]>;
}
let mayStore = 1 in {
def ST_L : RXPseudo<(outs), (ins GPR:$v, mem:$a),
                    [(store GPR:$v, AddrL:$a)]>;
def ST_B : RXPseudo<(outs), (ins GPR:$v, mem:$a),
                    [(truncstorei8 GPR:$v, AddrB:$a)]>;
def ST_W : RXPseudo<(outs), (ins GPR:$v, mem:$a),
                    [(truncstorei16 GPR:$v, AddrW:$a)]>;
}
}
def : Pat<(i32 (extloadi8 AddrB:$a)), (LD_UB AddrB:$a)>;
def : Pat<(i32 (extloadi16 AddrW:$a)), (LD_UW AddrW:$a)>;

// Naturally aligned accesses of up to 32 bits are single instructions, so
// atomic loads and stores are plain MOVs (RX parts are single-core; there is
// no compare-and-swap, so read-modify-write atomics are not supported). The
// atomic memory operands keep later passes from caching or hoisting them.
def : Pat<(i32 (atomic_load_nonext_32 AddrL:$a)), (LD_L AddrL:$a)>;
def : Pat<(i32 (atomic_load_zext_8 AddrB:$a)), (LD_UB AddrB:$a)>;
def : Pat<(i32 (atomic_load_aext_8 AddrB:$a)), (LD_UB AddrB:$a)>;
def : Pat<(i32 (atomic_load_sext_8 AddrB:$a)), (LD_B AddrB:$a)>;
def : Pat<(i32 (atomic_load_zext_16 AddrW:$a)), (LD_UW AddrW:$a)>;
def : Pat<(i32 (atomic_load_aext_16 AddrW:$a)), (LD_UW AddrW:$a)>;
def : Pat<(i32 (atomic_load_sext_16 AddrW:$a)), (LD_W AddrW:$a)>;
def : Pat<(atomic_store_32 GPR:$v, AddrL:$a), (ST_L GPR:$v, AddrL:$a)>;
def : Pat<(atomic_store_16 GPR:$v, AddrW:$a), (ST_W GPR:$v, AddrW:$a)>;
def : Pat<(atomic_store_8 GPR:$v, AddrB:$a), (ST_B GPR:$v, AddrB:$a)>;

// llvm.trap / llvm.debugtrap: BRK raises the unconditional trap exception.
def : Pat<(trap), (BRK)>;
def : Pat<(debugtrap), (BRK)>;

// Extensions within a register.
def : Pat<(sext_inreg GPR:$a, i8), (MOV_B_rr GPR:$a)>;
def : Pat<(sext_inreg GPR:$a, i16), (MOV_W_rr GPR:$a)>;
def : Pat<(and GPR:$a, 0xff), (MOVU_B_rr GPR:$a)>;
def : Pat<(and GPR:$a, 0xffff), (MOVU_W_rr GPR:$a)>;

// Arithmetic and logic. SUB rs, rs2, rd computes rs2 - rs.
def : Pat<(add GPR:$a, GPR:$b), (ADD_rrr GPR:$a, GPR:$b)>;
def : Pat<(add GPR:$a, imm:$i), (ADD_rri32 GPR:$a, imm:$i)>;
def : Pat<(sub GPR:$a, GPR:$b), (SUB_rrr GPR:$b, GPR:$a)>;
def : Pat<(mul GPR:$a, GPR:$b), (MUL_rrr GPR:$a, GPR:$b)>;
def : Pat<(mul GPR:$a, imm:$i), (MUL_ri32 GPR:$a, imm:$i)>;
def : Pat<(and GPR:$a, GPR:$b), (AND_rrr GPR:$a, GPR:$b)>;
def : Pat<(and GPR:$a, imm:$i), (AND_ri32 GPR:$a, imm:$i)>;
def : Pat<(or GPR:$a, GPR:$b), (OR_rrr GPR:$a, GPR:$b)>;
def : Pat<(or GPR:$a, imm:$i), (OR_ri32 GPR:$a, imm:$i)>;
def : Pat<(not GPR:$a), (NOT_r GPR:$a)>;
def : Pat<(xor GPR:$a, GPR:$b), (XOR_rr GPR:$a, GPR:$b)>;
def : Pat<(xor GPR:$a, imm:$i), (XOR_ri32 GPR:$a, imm:$i)>;
def : Pat<(ineg GPR:$a), (NEG_r GPR:$a)>;
def : Pat<(abs GPR:$a), (ABS_r GPR:$a)>;
def : Pat<(sdiv GPR:$a, GPR:$b), (DIV_rr GPR:$a, GPR:$b)>;
def : Pat<(udiv GPR:$a, GPR:$b), (DIVU_rr GPR:$a, GPR:$b)>;
def : Pat<(smax GPR:$a, GPR:$b), (MAX_rr GPR:$a, GPR:$b)>;
def : Pat<(smin GPR:$a, GPR:$b), (MIN_rr GPR:$a, GPR:$b)>;
def : Pat<(bswap GPR:$a), (REVL_rr GPR:$a)>;

// Shifts and rotates.
def : Pat<(shl GPR:$a, uimm5:$s), (SHLL_rri uimm5:$s, GPR:$a)>;
def : Pat<(srl GPR:$a, uimm5:$s), (SHLR_rri uimm5:$s, GPR:$a)>;
def : Pat<(sra GPR:$a, uimm5:$s), (SHAR_rri uimm5:$s, GPR:$a)>;
def : Pat<(shl GPR:$a, GPR:$s), (SHLL_rr GPR:$a, GPR:$s)>;
def : Pat<(srl GPR:$a, GPR:$s), (SHLR_rr GPR:$a, GPR:$s)>;
def : Pat<(sra GPR:$a, GPR:$s), (SHAR_rr GPR:$a, GPR:$s)>;
def : Pat<(rotl GPR:$a, uimm5:$s), (ROTL_ri GPR:$a, uimm5:$s)>;
def : Pat<(rotr GPR:$a, uimm5:$s), (ROTR_ri GPR:$a, uimm5:$s)>;
def : Pat<(rotl GPR:$a, GPR:$s), (ROTL_rr GPR:$a, GPR:$s)>;
def : Pat<(rotr GPR:$a, GPR:$s), (ROTR_rr GPR:$a, GPR:$s)>;

// Carry chains for i64 (and wider) arithmetic: the carry travels in PSW.C.
// RX's C after a subtraction means "no borrow" and SBB subtracts !C, so a
// SUB/SBB pair is self-consistent. There is no SUB #imm wider than 4 bits
// with the right carry, so subc only takes registers.
def : Pat<(addc GPR:$a, GPR:$b), (ADD_rrr GPR:$a, GPR:$b)>;
def : Pat<(addc GPR:$a, imm:$i), (ADD_rri32 GPR:$a, imm:$i)>;
def : Pat<(adde GPR:$a, GPR:$b), (ADC_rr GPR:$a, GPR:$b)>;
def : Pat<(adde GPR:$a, imm:$i), (ADC_ri32 GPR:$a, imm:$i)>;
def : Pat<(subc GPR:$a, GPR:$b), (SUB_rrr GPR:$b, GPR:$a)>;
def : Pat<(sube GPR:$a, GPR:$b), (SBB_rr GPR:$a, GPR:$b)>;

// Comparisons: cmp src, src2 sets the flags for src2 - src.
def : Pat<(RXcmp GPR:$a, GPR:$b), (CMP_rr GPR:$a, GPR:$b)>;
def : Pat<(RXcmp GPR:$a, imm:$i), (CMP_ri32 GPR:$a, imm:$i)>;
