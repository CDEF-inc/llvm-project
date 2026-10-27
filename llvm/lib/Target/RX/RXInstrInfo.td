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
// The immediates of mov.b / mov.w #imm, dsp[Rd]: .B takes -128..255 in its
// 8-bit field, .W values are taken modulo 2^16 (8 bits where they fit).
def ImmB8Class    : RXImmClass<"ImmB8">;
def ImmW16Class   : RXImmClass<"ImmW16">;
def ImmW8Class    : RXImmClass<"ImmW8", [ImmW16Class]>;

def immu4   : Operand<i32> { let ParserMatchClass = ImmU4Class; }
def immu5   : Operand<i32> { let ParserMatchClass = ImmU5Class; }
def immu8   : Operand<i32> { let ParserMatchClass = ImmU8Class; }
def imms8   : Operand<i32> { let ParserMatchClass = ImmS8Class; }
def imms8n  : Operand<i32> { let ParserMatchClass = ImmS8NegClass; }
def imms16  : Operand<i32> { let ParserMatchClass = ImmS16Class; }
def imms24  : Operand<i32> { let ParserMatchClass = ImmS24Class; }
def immb8   : Operand<i32> { let ParserMatchClass = ImmB8Class; }
def immw8   : Operand<i32> { let ParserMatchClass = ImmW8Class; }
def immw16  : Operand<i32> { let ParserMatchClass = ImmW16Class; }
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
// MOV / MOVU: pre-decrement / post-increment, register-indexed, immediate to
// memory and memory to memory
//===----------------------------------------------------------------------===//

// mov.sz Rs, [Rd+] / [-Rd] and mov.sz / movu.sz [Rs+] / [-Rs], Rd: the
// updated base register is an output tied to the input.
let mayStore = 1, Constraints = "$rd = $rd_wb" in
def MOV_B_stpi : RXInst24<(outs GPR:$rd_wb), (ins GPR:$rs, GPR:$rd),
                       "mov.b\t$rs, [${rd}+]"> {
  bits<4> rs;
  bits<4> rd;
  let Inst{23-16} = 0xfd;
  let Inst{15-8} = {0b00100, 0, 0b00};
  let Inst{7-4} = rd;
  let Inst{3-0} = rs;
}
let mayStore = 1, Constraints = "$rd = $rd_wb" in
def MOV_B_stpd : RXInst24<(outs GPR:$rd_wb), (ins GPR:$rs, GPR:$rd),
                       "mov.b\t$rs, [-$rd]"> {
  bits<4> rs;
  bits<4> rd;
  let Inst{23-16} = 0xfd;
  let Inst{15-8} = {0b00100, 1, 0b00};
  let Inst{7-4} = rd;
  let Inst{3-0} = rs;
}
let mayStore = 1, Constraints = "$rd = $rd_wb" in
def MOV_W_stpi : RXInst24<(outs GPR:$rd_wb), (ins GPR:$rs, GPR:$rd),
                       "mov.w\t$rs, [${rd}+]"> {
  bits<4> rs;
  bits<4> rd;
  let Inst{23-16} = 0xfd;
  let Inst{15-8} = {0b00100, 0, 0b01};
  let Inst{7-4} = rd;
  let Inst{3-0} = rs;
}
let mayStore = 1, Constraints = "$rd = $rd_wb" in
def MOV_W_stpd : RXInst24<(outs GPR:$rd_wb), (ins GPR:$rs, GPR:$rd),
                       "mov.w\t$rs, [-$rd]"> {
  bits<4> rs;
  bits<4> rd;
  let Inst{23-16} = 0xfd;
  let Inst{15-8} = {0b00100, 1, 0b01};
  let Inst{7-4} = rd;
  let Inst{3-0} = rs;
}
let mayStore = 1, Constraints = "$rd = $rd_wb" in
def MOV_L_stpi : RXInst24<(outs GPR:$rd_wb), (ins GPR:$rs, GPR:$rd),
                       "mov.l\t$rs, [${rd}+]"> {
  bits<4> rs;
  bits<4> rd;
  let Inst{23-16} = 0xfd;
  let Inst{15-8} = {0b00100, 0, 0b10};
  let Inst{7-4} = rd;
  let Inst{3-0} = rs;
}
let mayStore = 1, Constraints = "$rd = $rd_wb" in
def MOV_L_stpd : RXInst24<(outs GPR:$rd_wb), (ins GPR:$rs, GPR:$rd),
                       "mov.l\t$rs, [-$rd]"> {
  bits<4> rs;
  bits<4> rd;
  let Inst{23-16} = 0xfd;
  let Inst{15-8} = {0b00100, 1, 0b10};
  let Inst{7-4} = rd;
  let Inst{3-0} = rs;
}
let mayLoad = 1, Constraints = "$rs = $rs_wb" in
def MOV_B_ldpi : RXInst24<(outs GPR:$rd, GPR:$rs_wb), (ins GPR:$rs),
                       "mov.b\t[${rs}+], $rd"> {
  bits<4> rs;
  bits<4> rd;
  let Inst{23-16} = 0xfd;
  let Inst{15-8} = {0b00101, 0, 0b00};
  let Inst{7-4} = rs;
  let Inst{3-0} = rd;
}
let mayLoad = 1, Constraints = "$rs = $rs_wb" in
def MOV_B_ldpd : RXInst24<(outs GPR:$rd, GPR:$rs_wb), (ins GPR:$rs),
                       "mov.b\t[-$rs], $rd"> {
  bits<4> rs;
  bits<4> rd;
  let Inst{23-16} = 0xfd;
  let Inst{15-8} = {0b00101, 1, 0b00};
  let Inst{7-4} = rs;
  let Inst{3-0} = rd;
}
let mayLoad = 1, Constraints = "$rs = $rs_wb" in
def MOV_W_ldpi : RXInst24<(outs GPR:$rd, GPR:$rs_wb), (ins GPR:$rs),
                       "mov.w\t[${rs}+], $rd"> {
  bits<4> rs;
  bits<4> rd;
  let Inst{23-16} = 0xfd;
  let Inst{15-8} = {0b00101, 0, 0b01};
  let Inst{7-4} = rs;
  let Inst{3-0} = rd;
}
let mayLoad = 1, Constraints = "$rs = $rs_wb" in
def MOV_W_ldpd : RXInst24<(outs GPR:$rd, GPR:$rs_wb), (ins GPR:$rs),
                       "mov.w\t[-$rs], $rd"> {
  bits<4> rs;
  bits<4> rd;
  let Inst{23-16} = 0xfd;
  let Inst{15-8} = {0b00101, 1, 0b01};
  let Inst{7-4} = rs;
  let Inst{3-0} = rd;
}
let mayLoad = 1, Constraints = "$rs = $rs_wb" in
def MOV_L_ldpi : RXInst24<(outs GPR:$rd, GPR:$rs_wb), (ins GPR:$rs),
                       "mov.l\t[${rs}+], $rd"> {
  bits<4> rs;
  bits<4> rd;
  let Inst{23-16} = 0xfd;
  let Inst{15-8} = {0b00101, 0, 0b10};
  let Inst{7-4} = rs;
  let Inst{3-0} = rd;
}
let mayLoad = 1, Constraints = "$rs = $rs_wb" in
def MOV_L_ldpd : RXInst24<(outs GPR:$rd, GPR:$rs_wb), (ins GPR:$rs),
                       "mov.l\t[-$rs], $rd"> {
  bits<4> rs;
  bits<4> rd;
  let Inst{23-16} = 0xfd;
  let Inst{15-8} = {0b00101, 1, 0b10};
  let Inst{7-4} = rs;
  let Inst{3-0} = rd;
}
let mayLoad = 1, Constraints = "$rs = $rs_wb" in
def MOVU_B_ldpi : RXInst24<(outs GPR:$rd, GPR:$rs_wb), (ins GPR:$rs),
                       "movu.b\t[${rs}+], $rd"> {
  bits<4> rs;
  bits<4> rd;
  let Inst{23-16} = 0xfd;
  let Inst{15-8} = {0b00111, 0, {0, 0}};
  let Inst{7-4} = rs;
  let Inst{3-0} = rd;
}
let mayLoad = 1, Constraints = "$rs = $rs_wb" in
def MOVU_B_ldpd : RXInst24<(outs GPR:$rd, GPR:$rs_wb), (ins GPR:$rs),
                       "movu.b\t[-$rs], $rd"> {
  bits<4> rs;
  bits<4> rd;
  let Inst{23-16} = 0xfd;
  let Inst{15-8} = {0b00111, 1, {0, 0}};
  let Inst{7-4} = rs;
  let Inst{3-0} = rd;
}
let mayLoad = 1, Constraints = "$rs = $rs_wb" in
def MOVU_W_ldpi : RXInst24<(outs GPR:$rd, GPR:$rs_wb), (ins GPR:$rs),
                       "movu.w\t[${rs}+], $rd"> {
  bits<4> rs;
  bits<4> rd;
  let Inst{23-16} = 0xfd;
  let Inst{15-8} = {0b00111, 0, {0, 1}};
  let Inst{7-4} = rs;
  let Inst{3-0} = rd;
}
let mayLoad = 1, Constraints = "$rs = $rs_wb" in
def MOVU_W_ldpd : RXInst24<(outs GPR:$rd, GPR:$rs_wb), (ins GPR:$rs),
                       "movu.w\t[-$rs], $rd"> {
  bits<4> rs;
  bits<4> rd;
  let Inst{23-16} = 0xfd;
  let Inst{15-8} = {0b00111, 1, {0, 1}};
  let Inst{7-4} = rs;
  let Inst{3-0} = rd;
}

// mov.sz [Ri, Rb], Rd / Rs, [Ri, Rb] and movu.sz [Ri, Rb], Rd: the address
// is Rb + Ri * size.
let mayLoad = 1 in
def MOV_B_ldx : RXInst24<(outs GPR:$rd), (ins GPR:$ri, GPR:$rb),
                        "mov.b\t[$ri, $rb], $rd"> {
  bits<4> ri;
  bits<4> rb;
  bits<4> rd;
  let Inst{23-16} = 0xfe;
  let Inst{15-8} = {0b01, 0b00, ri};
  let Inst{7-4} = rb;
  let Inst{3-0} = rd;
}
let mayStore = 1 in
def MOV_B_stx : RXInst24<(outs), (ins GPR:$rs, GPR:$ri, GPR:$rb),
                        "mov.b\t$rs, [$ri, $rb]"> {
  bits<4> rs;
  bits<4> ri;
  bits<4> rb;
  let Inst{23-16} = 0xfe;
  let Inst{15-8} = {0b00, 0b00, ri};
  let Inst{7-4} = rb;
  let Inst{3-0} = rs;
}
let mayLoad = 1 in
def MOV_W_ldx : RXInst24<(outs GPR:$rd), (ins GPR:$ri, GPR:$rb),
                        "mov.w\t[$ri, $rb], $rd"> {
  bits<4> ri;
  bits<4> rb;
  bits<4> rd;
  let Inst{23-16} = 0xfe;
  let Inst{15-8} = {0b01, 0b01, ri};
  let Inst{7-4} = rb;
  let Inst{3-0} = rd;
}
let mayStore = 1 in
def MOV_W_stx : RXInst24<(outs), (ins GPR:$rs, GPR:$ri, GPR:$rb),
                        "mov.w\t$rs, [$ri, $rb]"> {
  bits<4> rs;
  bits<4> ri;
  bits<4> rb;
  let Inst{23-16} = 0xfe;
  let Inst{15-8} = {0b00, 0b01, ri};
  let Inst{7-4} = rb;
  let Inst{3-0} = rs;
}
let mayLoad = 1 in
def MOV_L_ldx : RXInst24<(outs GPR:$rd), (ins GPR:$ri, GPR:$rb),
                        "mov.l\t[$ri, $rb], $rd"> {
  bits<4> ri;
  bits<4> rb;
  bits<4> rd;
  let Inst{23-16} = 0xfe;
  let Inst{15-8} = {0b01, 0b10, ri};
  let Inst{7-4} = rb;
  let Inst{3-0} = rd;
}
let mayStore = 1 in
def MOV_L_stx : RXInst24<(outs), (ins GPR:$rs, GPR:$ri, GPR:$rb),
                        "mov.l\t$rs, [$ri, $rb]"> {
  bits<4> rs;
  bits<4> ri;
  bits<4> rb;
  let Inst{23-16} = 0xfe;
  let Inst{15-8} = {0b00, 0b10, ri};
  let Inst{7-4} = rb;
  let Inst{3-0} = rs;
}
let mayLoad = 1 in
def MOVU_B_ldx : RXInst24<(outs GPR:$rd), (ins GPR:$ri, GPR:$rb),
                         "movu.b\t[$ri, $rb], $rd"> {
  bits<4> ri;
  bits<4> rb;
  bits<4> rd;
  let Inst{23-16} = 0xfe;
  let Inst{15-8} = {0b110, 0, ri};
  let Inst{7-4} = rb;
  let Inst{3-0} = rd;
}
let mayLoad = 1 in
def MOVU_W_ldx : RXInst24<(outs GPR:$rd), (ins GPR:$ri, GPR:$rb),
                         "movu.w\t[$ri, $rb], $rd"> {
  bits<4> ri;
  bits<4> rb;
  bits<4> rd;
  let Inst{23-16} = 0xfe;
  let Inst{15-8} = {0b110, 1, ri};
  let Inst{7-4} = rb;
  let Inst{3-0} = rd;
}

// mov.sz #imm, dsp:5[Rd] (Rd = R0-R7): #imm is 0-255 (as GNU as uses it).
// The parser rewrites the dsp:8 forms to these where they fit.
let mayStore = 1 in
def MOV_B_mi5 : RXInst24<(outs), (ins GPRLow:$rd, dsp5b:$dsp, immu8:$imm),
                        "mov.b\t$imm, $dsp[$rd]"> {
  bits<3> rd;
  bits<5> dsp;
  bits<8> imm;
  let Inst{23-16} = {0b001111, 0b00};
  let Inst{15} = dsp{4};
  let Inst{14-12} = rd;
  let Inst{11-8} = dsp{3-0};
  let Inst{7-0} = imm;
}
let mayStore = 1 in
def MOV_W_mi5 : RXInst24<(outs), (ins GPRLow:$rd, dsp5w:$dsp, immu8:$imm),
                        "mov.w\t$imm, $dsp[$rd]"> {
  bits<3> rd;
  bits<5> dsp;
  bits<8> imm;
  let Inst{23-16} = {0b001111, 0b01};
  let Inst{15} = dsp{4};
  let Inst{14-12} = rd;
  let Inst{11-8} = dsp{3-0};
  let Inst{7-0} = imm;
}
let mayStore = 1 in
def MOV_L_mi5 : RXInst24<(outs), (ins GPRLow:$rd, dsp5l:$dsp, immu8:$imm),
                        "mov.l\t$imm, $dsp[$rd]"> {
  bits<3> rd;
  bits<5> dsp;
  bits<8> imm;
  let Inst{23-16} = {0b001111, 0b10};
  let Inst{15} = dsp{4};
  let Inst{14-12} = rd;
  let Inst{11-8} = dsp{3-0};
  let Inst{7-0} = imm;
}

// mov.sz #imm, [Rd] / dsp:8[Rd] / dsp:16[Rd]: 1111 10 ld, rd li sz, the
// displacement, then the immediate (li: 01 simm8, 10 simm16, 11 simm24,
// 00 imm32; .B takes 8 bits, .W 8 or 16).
let mayStore = 1 in
def MOV_B_mi0_b8 : RXInst24<(outs), (ins GPR:$rd, immb8:$imm),
                           "mov.b\t$imm, [$rd]"> {
  bits<4> rd;
  bits<8> imm;
  let Inst{23-16} = {0b111110, 0b00};
  let Inst{15-12} = rd;
  let Inst{11-8} = {0b01, 0b00};
  let Inst{7-0} = imm{7-0};
}
let mayStore = 1 in
def MOV_B_mi8_b8 : RXInst32<(outs), (ins GPR:$rd, dsp8b:$dsp, immb8:$imm),
                           "mov.b\t$imm, $dsp[$rd]"> {
  bits<4> rd;
  bits<8> dsp;
  bits<8> imm;
  let Inst{31-24} = {0b111110, 0b01};
  let Inst{23-20} = rd;
  let Inst{19-16} = {0b01, 0b00};
  let Inst{15-8} = dsp{7-0};
  let Inst{7-0} = imm{7-0};
}
let mayStore = 1 in
def MOV_B_mi16_b8 : RXInst40<(outs), (ins GPR:$rd, dsp16b:$dsp, immb8:$imm),
                           "mov.b\t$imm, $dsp[$rd]"> {
  bits<4> rd;
  bits<16> dsp;
  bits<8> imm;
  let Inst{39-32} = {0b111110, 0b10};
  let Inst{31-28} = rd;
  let Inst{27-24} = {0b01, 0b00};
  let Inst{23-16} = dsp{7-0};
  let Inst{15-8} = dsp{15-8};
  let Inst{7-0} = imm{7-0};
}
let mayStore = 1 in
def MOV_W_mi0_w8 : RXInst24<(outs), (ins GPR:$rd, immw8:$imm),
                           "mov.w\t$imm, [$rd]"> {
  bits<4> rd;
  bits<8> imm;
  let Inst{23-16} = {0b111110, 0b00};
  let Inst{15-12} = rd;
  let Inst{11-8} = {0b01, 0b01};
  let Inst{7-0} = imm{7-0};
}
let mayStore = 1 in
def MOV_W_mi8_w8 : RXInst32<(outs), (ins GPR:$rd, dsp8w:$dsp, immw8:$imm),
                           "mov.w\t$imm, $dsp[$rd]"> {
  bits<4> rd;
  bits<8> dsp;
  bits<8> imm;
  let Inst{31-24} = {0b111110, 0b01};
  let Inst{23-20} = rd;
  let Inst{19-16} = {0b01, 0b01};
  let Inst{15-8} = dsp{7-0};
  let Inst{7-0} = imm{7-0};
}
let mayStore = 1 in
def MOV_W_mi16_w8 : RXInst40<(outs), (ins GPR:$rd, dsp16w:$dsp, immw8:$imm),
                           "mov.w\t$imm, $dsp[$rd]"> {
  bits<4> rd;
  bits<16> dsp;
  bits<8> imm;
  let Inst{39-32} = {0b111110, 0b10};
  let Inst{31-28} = rd;
  let Inst{27-24} = {0b01, 0b01};
  let Inst{23-16} = dsp{7-0};
  let Inst{15-8} = dsp{15-8};
  let Inst{7-0} = imm{7-0};
}
let mayStore = 1 in
def MOV_W_mi0_w16 : RXInst32<(outs), (ins GPR:$rd, immw16:$imm),
                           "mov.w\t$imm, [$rd]"> {
  bits<4> rd;
  bits<16> imm;
  let Inst{31-24} = {0b111110, 0b00};
  let Inst{23-20} = rd;
  let Inst{19-16} = {0b10, 0b01};
  let Inst{15-8} = imm{7-0};
  let Inst{7-0} = imm{15-8};
}
let mayStore = 1 in
def MOV_W_mi8_w16 : RXInst40<(outs), (ins GPR:$rd, dsp8w:$dsp, immw16:$imm),
                           "mov.w\t$imm, $dsp[$rd]"> {
  bits<4> rd;
  bits<8> dsp;
  bits<16> imm;
  let Inst{39-32} = {0b111110, 0b01};
  let Inst{31-28} = rd;
  let Inst{27-24} = {0b10, 0b01};
  let Inst{23-16} = dsp{7-0};
  let Inst{15-8} = imm{7-0};
  let Inst{7-0} = imm{15-8};
}
let mayStore = 1 in
def MOV_W_mi16_w16 : RXInst48<(outs), (ins GPR:$rd, dsp16w:$dsp, immw16:$imm),
                           "mov.w\t$imm, $dsp[$rd]"> {
  bits<4> rd;
  bits<16> dsp;
  bits<16> imm;
  let Inst{47-40} = {0b111110, 0b10};
  let Inst{39-36} = rd;
  let Inst{35-32} = {0b10, 0b01};
  let Inst{31-24} = dsp{7-0};
  let Inst{23-16} = dsp{15-8};
  let Inst{15-8} = imm{7-0};
  let Inst{7-0} = imm{15-8};
}
let mayStore = 1 in
def MOV_L_mi0_s8 : RXInst24<(outs), (ins GPR:$rd, imms8:$imm),
                           "mov.l\t$imm, [$rd]"> {
  bits<4> rd;
  bits<8> imm;
  let Inst{23-16} = {0b111110, 0b00};
  let Inst{15-12} = rd;
  let Inst{11-8} = {0b01, 0b10};
  let Inst{7-0} = imm{7-0};
}
let mayStore = 1 in
def MOV_L_mi8_s8 : RXInst32<(outs), (ins GPR:$rd, dsp8l:$dsp, imms8:$imm),
                           "mov.l\t$imm, $dsp[$rd]"> {
  bits<4> rd;
  bits<8> dsp;
  bits<8> imm;
  let Inst{31-24} = {0b111110, 0b01};
  let Inst{23-20} = rd;
  let Inst{19-16} = {0b01, 0b10};
  let Inst{15-8} = dsp{7-0};
  let Inst{7-0} = imm{7-0};
}
let mayStore = 1 in
def MOV_L_mi16_s8 : RXInst40<(outs), (ins GPR:$rd, dsp16l:$dsp, imms8:$imm),
                           "mov.l\t$imm, $dsp[$rd]"> {
  bits<4> rd;
  bits<16> dsp;
  bits<8> imm;
  let Inst{39-32} = {0b111110, 0b10};
  let Inst{31-28} = rd;
  let Inst{27-24} = {0b01, 0b10};
  let Inst{23-16} = dsp{7-0};
  let Inst{15-8} = dsp{15-8};
  let Inst{7-0} = imm{7-0};
}
let mayStore = 1 in
def MOV_L_mi0_s16 : RXInst32<(outs), (ins GPR:$rd, imms16:$imm),
                           "mov.l\t$imm, [$rd]"> {
  bits<4> rd;
  bits<16> imm;
  let Inst{31-24} = {0b111110, 0b00};
  let Inst{23-20} = rd;
  let Inst{19-16} = {0b10, 0b10};
  let Inst{15-8} = imm{7-0};
  let Inst{7-0} = imm{15-8};
}
let mayStore = 1 in
def MOV_L_mi8_s16 : RXInst40<(outs), (ins GPR:$rd, dsp8l:$dsp, imms16:$imm),
                           "mov.l\t$imm, $dsp[$rd]"> {
  bits<4> rd;
  bits<8> dsp;
  bits<16> imm;
  let Inst{39-32} = {0b111110, 0b01};
  let Inst{31-28} = rd;
  let Inst{27-24} = {0b10, 0b10};
  let Inst{23-16} = dsp{7-0};
  let Inst{15-8} = imm{7-0};
  let Inst{7-0} = imm{15-8};
}
let mayStore = 1 in
def MOV_L_mi16_s16 : RXInst48<(outs), (ins GPR:$rd, dsp16l:$dsp, imms16:$imm),
                           "mov.l\t$imm, $dsp[$rd]"> {
  bits<4> rd;
  bits<16> dsp;
  bits<16> imm;
  let Inst{47-40} = {0b111110, 0b10};
  let Inst{39-36} = rd;
  let Inst{35-32} = {0b10, 0b10};
  let Inst{31-24} = dsp{7-0};
  let Inst{23-16} = dsp{15-8};
  let Inst{15-8} = imm{7-0};
  let Inst{7-0} = imm{15-8};
}
let mayStore = 1 in
def MOV_L_mi0_s24 : RXInst40<(outs), (ins GPR:$rd, imms24:$imm),
                           "mov.l\t$imm, [$rd]"> {
  bits<4> rd;
  bits<24> imm;
  let Inst{39-32} = {0b111110, 0b00};
  let Inst{31-28} = rd;
  let Inst{27-24} = {0b11, 0b10};
  let Inst{23-16} = imm{7-0};
  let Inst{15-8} = imm{15-8};
  let Inst{7-0} = imm{23-16};
}
let mayStore = 1 in
def MOV_L_mi8_s24 : RXInst48<(outs), (ins GPR:$rd, dsp8l:$dsp, imms24:$imm),
                           "mov.l\t$imm, $dsp[$rd]"> {
  bits<4> rd;
  bits<8> dsp;
  bits<24> imm;
  let Inst{47-40} = {0b111110, 0b01};
  let Inst{39-36} = rd;
  let Inst{35-32} = {0b11, 0b10};
  let Inst{31-24} = dsp{7-0};
  let Inst{23-16} = imm{7-0};
  let Inst{15-8} = imm{15-8};
  let Inst{7-0} = imm{23-16};
}
let mayStore = 1 in
def MOV_L_mi16_s24 : RXInst56<(outs), (ins GPR:$rd, dsp16l:$dsp, imms24:$imm),
                           "mov.l\t$imm, $dsp[$rd]"> {
  bits<4> rd;
  bits<16> dsp;
  bits<24> imm;
  let Inst{55-48} = {0b111110, 0b10};
  let Inst{47-44} = rd;
  let Inst{43-40} = {0b11, 0b10};
  let Inst{39-32} = dsp{7-0};
  let Inst{31-24} = dsp{15-8};
  let Inst{23-16} = imm{7-0};
  let Inst{15-8} = imm{15-8};
  let Inst{7-0} = imm{23-16};
}
let mayStore = 1 in
def MOV_L_mi0_i32 : RXInst48<(outs), (ins GPR:$rd, imm32:$imm),
                           "mov.l\t$imm, [$rd]"> {
  bits<4> rd;
  bits<32> imm;
  let Inst{47-40} = {0b111110, 0b00};
  let Inst{39-36} = rd;
  let Inst{35-32} = {0b00, 0b10};
  let Inst{31-24} = imm{7-0};
  let Inst{23-16} = imm{15-8};
  let Inst{15-8} = imm{23-16};
  let Inst{7-0} = imm{31-24};
}
let mayStore = 1 in
def MOV_L_mi8_i32 : RXInst56<(outs), (ins GPR:$rd, dsp8l:$dsp, imm32:$imm),
                           "mov.l\t$imm, $dsp[$rd]"> {
  bits<4> rd;
  bits<8> dsp;
  bits<32> imm;
  let Inst{55-48} = {0b111110, 0b01};
  let Inst{47-44} = rd;
  let Inst{43-40} = {0b00, 0b10};
  let Inst{39-32} = dsp{7-0};
  let Inst{31-24} = imm{7-0};
  let Inst{23-16} = imm{15-8};
  let Inst{15-8} = imm{23-16};
  let Inst{7-0} = imm{31-24};
}
let mayStore = 1 in
def MOV_L_mi16_i32 : RXInst64<(outs), (ins GPR:$rd, dsp16l:$dsp, imm32:$imm),
                           "mov.l\t$imm, $dsp[$rd]"> {
  bits<4> rd;
  bits<16> dsp;
  bits<32> imm;
  let Inst{63-56} = {0b111110, 0b10};
  let Inst{55-52} = rd;
  let Inst{51-48} = {0b00, 0b10};
  let Inst{47-40} = dsp{7-0};
  let Inst{39-32} = dsp{15-8};
  let Inst{31-24} = imm{7-0};
  let Inst{23-16} = imm{15-8};
  let Inst{15-8} = imm{23-16};
  let Inst{7-0} = imm{31-24};
}

// mov.sz src, dest between memory: 11 sz ldd lds, rs rd, the source's
// displacement, then the destination's.
let mayLoad = 1, mayStore = 1 in
def MOV_B_mm0_0 : RXInst16<(outs), (ins GPR:$rs, GPR:$rd),
                          "mov.b\t[$rs], [$rd]"> {
  bits<4> rs;
  bits<4> rd;
  let Inst{15-8} = {0b11, 0b00, 0b00, 0b00};
  let Inst{7-4} = rs;
  let Inst{3-0} = rd;
}
let mayLoad = 1, mayStore = 1 in
def MOV_B_mm0_8 : RXInst24<(outs), (ins GPR:$rs, GPR:$rd, dsp8b:$ddsp),
                          "mov.b\t[$rs], $ddsp[$rd]"> {
  bits<4> rs;
  bits<4> rd;
  bits<8> ddsp;
  let Inst{23-16} = {0b11, 0b00, 0b01, 0b00};
  let Inst{15-12} = rs;
  let Inst{11-8} = rd;
  let Inst{7-0} = ddsp{7-0};
}
let mayLoad = 1, mayStore = 1 in
def MOV_B_mm0_16 : RXInst32<(outs), (ins GPR:$rs, GPR:$rd, dsp16b:$ddsp),
                          "mov.b\t[$rs], $ddsp[$rd]"> {
  bits<4> rs;
  bits<4> rd;
  bits<16> ddsp;
  let Inst{31-24} = {0b11, 0b00, 0b10, 0b00};
  let Inst{23-20} = rs;
  let Inst{19-16} = rd;
  let Inst{15-8} = ddsp{7-0};
  let Inst{7-0} = ddsp{15-8};
}
let mayLoad = 1, mayStore = 1 in
def MOV_B_mm8_0 : RXInst24<(outs), (ins GPR:$rs, dsp8b:$sdsp, GPR:$rd),
                          "mov.b\t$sdsp[$rs], [$rd]"> {
  bits<4> rs;
  bits<4> rd;
  bits<8> sdsp;
  let Inst{23-16} = {0b11, 0b00, 0b00, 0b01};
  let Inst{15-12} = rs;
  let Inst{11-8} = rd;
  let Inst{7-0} = sdsp{7-0};
}
let mayLoad = 1, mayStore = 1 in
def MOV_B_mm8_8 : RXInst32<(outs), (ins GPR:$rs, dsp8b:$sdsp, GPR:$rd, dsp8b:$ddsp),
                          "mov.b\t$sdsp[$rs], $ddsp[$rd]"> {
  bits<4> rs;
  bits<4> rd;
  bits<8> sdsp;
  bits<8> ddsp;
  let Inst{31-24} = {0b11, 0b00, 0b01, 0b01};
  let Inst{23-20} = rs;
  let Inst{19-16} = rd;
  let Inst{15-8} = sdsp{7-0};
  let Inst{7-0} = ddsp{7-0};
}
let mayLoad = 1, mayStore = 1 in
def MOV_B_mm8_16 : RXInst40<(outs), (ins GPR:$rs, dsp8b:$sdsp, GPR:$rd, dsp16b:$ddsp),
                          "mov.b\t$sdsp[$rs], $ddsp[$rd]"> {
  bits<4> rs;
  bits<4> rd;
  bits<8> sdsp;
  bits<16> ddsp;
  let Inst{39-32} = {0b11, 0b00, 0b10, 0b01};
  let Inst{31-28} = rs;
  let Inst{27-24} = rd;
  let Inst{23-16} = sdsp{7-0};
  let Inst{15-8} = ddsp{7-0};
  let Inst{7-0} = ddsp{15-8};
}
let mayLoad = 1, mayStore = 1 in
def MOV_B_mm16_0 : RXInst32<(outs), (ins GPR:$rs, dsp16b:$sdsp, GPR:$rd),
                          "mov.b\t$sdsp[$rs], [$rd]"> {
  bits<4> rs;
  bits<4> rd;
  bits<16> sdsp;
  let Inst{31-24} = {0b11, 0b00, 0b00, 0b10};
  let Inst{23-20} = rs;
  let Inst{19-16} = rd;
  let Inst{15-8} = sdsp{7-0};
  let Inst{7-0} = sdsp{15-8};
}
let mayLoad = 1, mayStore = 1 in
def MOV_B_mm16_8 : RXInst40<(outs), (ins GPR:$rs, dsp16b:$sdsp, GPR:$rd, dsp8b:$ddsp),
                          "mov.b\t$sdsp[$rs], $ddsp[$rd]"> {
  bits<4> rs;
  bits<4> rd;
  bits<16> sdsp;
  bits<8> ddsp;
  let Inst{39-32} = {0b11, 0b00, 0b01, 0b10};
  let Inst{31-28} = rs;
  let Inst{27-24} = rd;
  let Inst{23-16} = sdsp{7-0};
  let Inst{15-8} = sdsp{15-8};
  let Inst{7-0} = ddsp{7-0};
}
let mayLoad = 1, mayStore = 1 in
def MOV_B_mm16_16 : RXInst48<(outs), (ins GPR:$rs, dsp16b:$sdsp, GPR:$rd, dsp16b:$ddsp),
                          "mov.b\t$sdsp[$rs], $ddsp[$rd]"> {
  bits<4> rs;
  bits<4> rd;
  bits<16> sdsp;
  bits<16> ddsp;
  let Inst{47-40} = {0b11, 0b00, 0b10, 0b10};
  let Inst{39-36} = rs;
  let Inst{35-32} = rd;
  let Inst{31-24} = sdsp{7-0};
  let Inst{23-16} = sdsp{15-8};
  let Inst{15-8} = ddsp{7-0};
  let Inst{7-0} = ddsp{15-8};
}
let mayLoad = 1, mayStore = 1 in
def MOV_W_mm0_0 : RXInst16<(outs), (ins GPR:$rs, GPR:$rd),
                          "mov.w\t[$rs], [$rd]"> {
  bits<4> rs;
  bits<4> rd;
  let Inst{15-8} = {0b11, 0b01, 0b00, 0b00};
  let Inst{7-4} = rs;
  let Inst{3-0} = rd;
}
let mayLoad = 1, mayStore = 1 in
def MOV_W_mm0_8 : RXInst24<(outs), (ins GPR:$rs, GPR:$rd, dsp8w:$ddsp),
                          "mov.w\t[$rs], $ddsp[$rd]"> {
  bits<4> rs;
  bits<4> rd;
  bits<8> ddsp;
  let Inst{23-16} = {0b11, 0b01, 0b01, 0b00};
  let Inst{15-12} = rs;
  let Inst{11-8} = rd;
  let Inst{7-0} = ddsp{7-0};
}
let mayLoad = 1, mayStore = 1 in
def MOV_W_mm0_16 : RXInst32<(outs), (ins GPR:$rs, GPR:$rd, dsp16w:$ddsp),
                          "mov.w\t[$rs], $ddsp[$rd]"> {
  bits<4> rs;
  bits<4> rd;
  bits<16> ddsp;
  let Inst{31-24} = {0b11, 0b01, 0b10, 0b00};
  let Inst{23-20} = rs;
  let Inst{19-16} = rd;
  let Inst{15-8} = ddsp{7-0};
  let Inst{7-0} = ddsp{15-8};
}
let mayLoad = 1, mayStore = 1 in
def MOV_W_mm8_0 : RXInst24<(outs), (ins GPR:$rs, dsp8w:$sdsp, GPR:$rd),
                          "mov.w\t$sdsp[$rs], [$rd]"> {
  bits<4> rs;
  bits<4> rd;
  bits<8> sdsp;
  let Inst{23-16} = {0b11, 0b01, 0b00, 0b01};
  let Inst{15-12} = rs;
  let Inst{11-8} = rd;
  let Inst{7-0} = sdsp{7-0};
}
let mayLoad = 1, mayStore = 1 in
def MOV_W_mm8_8 : RXInst32<(outs), (ins GPR:$rs, dsp8w:$sdsp, GPR:$rd, dsp8w:$ddsp),
                          "mov.w\t$sdsp[$rs], $ddsp[$rd]"> {
  bits<4> rs;
  bits<4> rd;
  bits<8> sdsp;
  bits<8> ddsp;
  let Inst{31-24} = {0b11, 0b01, 0b01, 0b01};
  let Inst{23-20} = rs;
  let Inst{19-16} = rd;
  let Inst{15-8} = sdsp{7-0};
  let Inst{7-0} = ddsp{7-0};
}
let mayLoad = 1, mayStore = 1 in
def MOV_W_mm8_16 : RXInst40<(outs), (ins GPR:$rs, dsp8w:$sdsp, GPR:$rd, dsp16w:$ddsp),
                          "mov.w\t$sdsp[$rs], $ddsp[$rd]"> {
  bits<4> rs;
  bits<4> rd;
  bits<8> sdsp;
  bits<16> ddsp;
  let Inst{39-32} = {0b11, 0b01, 0b10, 0b01};
  let Inst{31-28} = rs;
  let Inst{27-24} = rd;
  let Inst{23-16} = sdsp{7-0};
  let Inst{15-8} = ddsp{7-0};
  let Inst{7-0} = ddsp{15-8};
}
let mayLoad = 1, mayStore = 1 in
def MOV_W_mm16_0 : RXInst32<(outs), (ins GPR:$rs, dsp16w:$sdsp, GPR:$rd),
                          "mov.w\t$sdsp[$rs], [$rd]"> {
  bits<4> rs;
  bits<4> rd;
  bits<16> sdsp;
  let Inst{31-24} = {0b11, 0b01, 0b00, 0b10};
  let Inst{23-20} = rs;
  let Inst{19-16} = rd;
  let Inst{15-8} = sdsp{7-0};
  let Inst{7-0} = sdsp{15-8};
}
let mayLoad = 1, mayStore = 1 in
def MOV_W_mm16_8 : RXInst40<(outs), (ins GPR:$rs, dsp16w:$sdsp, GPR:$rd, dsp8w:$ddsp),
                          "mov.w\t$sdsp[$rs], $ddsp[$rd]"> {
  bits<4> rs;
  bits<4> rd;
  bits<16> sdsp;
  bits<8> ddsp;
  let Inst{39-32} = {0b11, 0b01, 0b01, 0b10};
  let Inst{31-28} = rs;
  let Inst{27-24} = rd;
  let Inst{23-16} = sdsp{7-0};
  let Inst{15-8} = sdsp{15-8};
  let Inst{7-0} = ddsp{7-0};
}
let mayLoad = 1, mayStore = 1 in
def MOV_W_mm16_16 : RXInst48<(outs), (ins GPR:$rs, dsp16w:$sdsp, GPR:$rd, dsp16w:$ddsp),
                          "mov.w\t$sdsp[$rs], $ddsp[$rd]"> {
  bits<4> rs;
  bits<4> rd;
  bits<16> sdsp;
  bits<16> ddsp;
  let Inst{47-40} = {0b11, 0b01, 0b10, 0b10};
  let Inst{39-36} = rs;
  let Inst{35-32} = rd;
  let Inst{31-24} = sdsp{7-0};
  let Inst{23-16} = sdsp{15-8};
  let Inst{15-8} = ddsp{7-0};
  let Inst{7-0} = ddsp{15-8};
}
let mayLoad = 1, mayStore = 1 in
def MOV_L_mm0_0 : RXInst16<(outs), (ins GPR:$rs, GPR:$rd),
                          "mov.l\t[$rs], [$rd]"> {
  bits<4> rs;
  bits<4> rd;
  let Inst{15-8} = {0b11, 0b10, 0b00, 0b00};
  let Inst{7-4} = rs;
  let Inst{3-0} = rd;
}
let mayLoad = 1, mayStore = 1 in
def MOV_L_mm0_8 : RXInst24<(outs), (ins GPR:$rs, GPR:$rd, dsp8l:$ddsp),
                          "mov.l\t[$rs], $ddsp[$rd]"> {
  bits<4> rs;
  bits<4> rd;
  bits<8> ddsp;
  let Inst{23-16} = {0b11, 0b10, 0b01, 0b00};
  let Inst{15-12} = rs;
  let Inst{11-8} = rd;
  let Inst{7-0} = ddsp{7-0};
}
let mayLoad = 1, mayStore = 1 in
def MOV_L_mm0_16 : RXInst32<(outs), (ins GPR:$rs, GPR:$rd, dsp16l:$ddsp),
                          "mov.l\t[$rs], $ddsp[$rd]"> {
  bits<4> rs;
  bits<4> rd;
  bits<16> ddsp;
  let Inst{31-24} = {0b11, 0b10, 0b10, 0b00};
  let Inst{23-20} = rs;
  let Inst{19-16} = rd;
  let Inst{15-8} = ddsp{7-0};
  let Inst{7-0} = ddsp{15-8};
}
let mayLoad = 1, mayStore = 1 in
def MOV_L_mm8_0 : RXInst24<(outs), (ins GPR:$rs, dsp8l:$sdsp, GPR:$rd),
                          "mov.l\t$sdsp[$rs], [$rd]"> {
  bits<4> rs;
  bits<4> rd;
  bits<8> sdsp;
  let Inst{23-16} = {0b11, 0b10, 0b00, 0b01};
  let Inst{15-12} = rs;
  let Inst{11-8} = rd;
  let Inst{7-0} = sdsp{7-0};
}
let mayLoad = 1, mayStore = 1 in
def MOV_L_mm8_8 : RXInst32<(outs), (ins GPR:$rs, dsp8l:$sdsp, GPR:$rd, dsp8l:$ddsp),
                          "mov.l\t$sdsp[$rs], $ddsp[$rd]"> {
  bits<4> rs;
  bits<4> rd;
  bits<8> sdsp;
  bits<8> ddsp;
  let Inst{31-24} = {0b11, 0b10, 0b01, 0b01};
  let Inst{23-20} = rs;
  let Inst{19-16} = rd;
  let Inst{15-8} = sdsp{7-0};
  let Inst{7-0} = ddsp{7-0};
}
let mayLoad = 1, mayStore = 1 in
def MOV_L_mm8_16 : RXInst40<(outs), (ins GPR:$rs, dsp8l:$sdsp, GPR:$rd, dsp16l:$ddsp),
                          "mov.l\t$sdsp[$rs], $ddsp[$rd]"> {
  bits<4> rs;
  bits<4> rd;
  bits<8> sdsp;
  bits<16> ddsp;
  let Inst{39-32} = {0b11, 0b10, 0b10, 0b01};
  let Inst{31-28} = rs;
  let Inst{27-24} = rd;
  let Inst{23-16} = sdsp{7-0};
  let Inst{15-8} = ddsp{7-0};
  let Inst{7-0} = ddsp{15-8};
}
let mayLoad = 1, mayStore = 1 in
def MOV_L_mm16_0 : RXInst32<(outs), (ins GPR:$rs, dsp16l:$sdsp, GPR:$rd),
                          "mov.l\t$sdsp[$rs], [$rd]"> {
  bits<4> rs;
  bits<4> rd;
  bits<16> sdsp;
  let Inst{31-24} = {0b11, 0b10, 0b00, 0b10};
  let Inst{23-20} = rs;
  let Inst{19-16} = rd;
  let Inst{15-8} = sdsp{7-0};
  let Inst{7-0} = sdsp{15-8};
}
let mayLoad = 1, mayStore = 1 in
def MOV_L_mm16_8 : RXInst40<(outs), (ins GPR:$rs, dsp16l:$sdsp, GPR:$rd, dsp8l:$ddsp),
                          "mov.l\t$sdsp[$rs], $ddsp[$rd]"> {
  bits<4> rs;
  bits<4> rd;
  bits<16> sdsp;
  bits<8> ddsp;
  let Inst{39-32} = {0b11, 0b10, 0b01, 0b10};
  let Inst{31-28} = rs;
  let Inst{27-24} = rd;
  let Inst{23-16} = sdsp{7-0};
  let Inst{15-8} = sdsp{15-8};
  let Inst{7-0} = ddsp{7-0};
}
let mayLoad = 1, mayStore = 1 in
def MOV_L_mm16_16 : RXInst48<(outs), (ins GPR:$rs, dsp16l:$sdsp, GPR:$rd, dsp16l:$ddsp),
                          "mov.l\t$sdsp[$rs], $ddsp[$rd]"> {
  bits<4> rs;
  bits<4> rd;
  bits<16> sdsp;
  bits<16> ddsp;
  let Inst{47-40} = {0b11, 0b10, 0b10, 0b10};
  let Inst{39-36} = rs;
  let Inst{35-32} = rd;
  let Inst{31-24} = sdsp{7-0};
  let Inst{23-16} = sdsp{15-8};
  let Inst{15-8} = ddsp{7-0};
  let Inst{7-0} = ddsp{15-8};
}

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
// mvtc #imm, cr:   1111 1101 0111 li11 0000 cr imm
// trmvtc #imm, cr: 1111 1101 0111 li11 0010 cr imm (RXv3e)
multiclass MvtcImm<bits<4> op, RegisterClass rc, string mn> {
  def _i8 : RXInst32<(outs rc:$cr), (ins imms8:$imm), mn#"\t$imm, $cr"> {
    bits<4> cr;
    bits<8> imm;
    let Inst{31-24} = 0xfd;
    let Inst{23-16} = 0x77;
    let Inst{15-8} = {op, cr};
    let Inst{7-0} = imm;
  }
  def _i16 : RXInst40<(outs rc:$cr), (ins imms16:$imm), mn#"\t$imm, $cr"> {
    bits<4> cr;
    bits<16> imm;
    let Inst{39-32} = 0xfd;
    let Inst{31-24} = 0x7b;
    let Inst{23-16} = {op, cr};
    let Inst{15-8} = imm{7-0};
    let Inst{7-0} = imm{15-8};
  }
  def _i24 : RXInst48<(outs rc:$cr), (ins imms24:$imm), mn#"\t$imm, $cr"> {
    bits<4> cr;
    bits<24> imm;
    let Inst{47-40} = 0xfd;
    let Inst{39-32} = 0x7f;
    let Inst{31-24} = {op, cr};
    let Inst{23-16} = imm{7-0};
    let Inst{15-8} = imm{15-8};
    let Inst{7-0} = imm{23-16};
  }
  def _i32 : RXInst56<(outs rc:$cr), (ins imm32:$imm), mn#"\t$imm, $cr"> {
    bits<4> cr;
    bits<32> imm;
    let Inst{55-48} = 0xfd;
    let Inst{47-40} = 0x73;
    let Inst{39-32} = {op, cr};
    let Inst{31-24} = imm{7-0};
    let Inst{23-16} = imm{15-8};
    let Inst{15-8} = imm{23-16};
    let Inst{7-0} = imm{31-24};
  }
}
defm MVTC : MvtcImm<0b0000, CTRL, "mvtc">;

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

// RXv3e Protect Zone, interrupt priority levels 0-31 and INT Rs.
let Predicates = [HasRXv3e] in {
// mvtipl #imm: 0111 0101 0111 0000 000 imm[5]
def MVTIPL5 : RXInst24<(outs), (ins immu5:$imm), "mvtipl\t$imm"> {
  bits<5> imm;
  let Inst{23-8} = 0x7570;
  let Inst{7-5} = 0b000;
  let Inst{4-0} = imm;
}
// int Rs: 1111 1101 0111 0101 0001 rs 0000 0000
def INT_r : RXInst32<(outs), (ins GPR:$rs), "int\t$rs"> {
  bits<4> rs;
  let Inst{31-16} = 0xfd75;
  let Inst{15-12} = 0x1;
  let Inst{11-8} = rs;
  let Inst{7-0} = 0x00;
}
// trcall #imm: 1111 1101 0111 0101 0000 0000 imm[8]
def TRCALL : RXInst32<(outs), (ins immu8:$imm), "trcall\t$imm"> {
  bits<8> imm;
  let Inst{31-8} = 0xfd7500;
  let Inst{7-0} = imm;
}
def TRRTE : RXInst2<0x7f97, "trrte">;
// trmvtc Rs, cr: 1111 1101 0101 1000 rs cr
def TRMVTC_r : RXInst24<(outs TRCTRL:$cr), (ins GPR:$rs),
                        "trmvtc\t$rs, $cr"> {
  bits<4> rs;
  bits<4> cr;
  let Inst{23-8} = 0xfd58;
  let Inst{7-4} = rs;
  let Inst{3-0} = cr;
}
// trmvfc cr, Rd: 1111 1101 0101 1010 cr rd
def TRMVFC : RXInst24<(outs GPR:$rd), (ins TRCTRL:$cr),
                      "trmvfc\t$cr, $rd"> {
  bits<4> rd;
  bits<4> cr;
  let Inst{23-8} = 0xfd5a;
  let Inst{7-4} = cr;
  let Inst{3-0} = rd;
}
defm TRMVTC : MvtcImm<0b0010, TRCTRL, "trmvtc">;
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

// String instructions: R1 = destination, R2 = source, R3 = count.
let mayLoad = 1, mayStore = 1, Uses = [R1, R2, R3], Defs = [R1, R2, R3] in {
def SMOVF : RXInst2<0x7f8f, "smovf">;
def SMOVB : RXInst2<0x7f8b, "smovb">;
def SMOVU : RXInst2<0x7f87, "smovu">;
}
let mayLoad = 1, Uses = [R1, R2, R3], Defs = [R1, R2, R3, PSW] in
def SCMPU : RXInst2<0x7f83, "scmpu">;
let mayStore = 1, Uses = [R1, R2, R3], Defs = [R1, R3] in {
def SSTR_B : RXInst2<0x7f88, "sstr.b">;
def SSTR_W : RXInst2<0x7f89, "sstr.w">;
def SSTR_L : RXInst2<0x7f8a, "sstr.l">;
}
let mayLoad = 1, Uses = [R1, R2, R3], Defs = [R1, R2, R3, PSW] in {
def SUNTIL_B : RXInst2<0x7f80, "suntil.b">;
def SUNTIL_W : RXInst2<0x7f81, "suntil.w">;
def SUNTIL_L : RXInst2<0x7f82, "suntil.l">;
def SWHILE_B : RXInst2<0x7f84, "swhile.b">;
def SWHILE_W : RXInst2<0x7f85, "swhile.w">;
def SWHILE_L : RXInst2<0x7f86, "swhile.l">;
}
// rmpa: R6:R5:R4 += sum of [R1] * [R2] over R3 elements; also changes ACC0.
let mayLoad = 1, Uses = [R1, R2, R3, R4, R5, R6],
    Defs = [R1, R2, R3, R4, R5, R6, ACC0, PSW] in {
def RMPA_B : RXInst2<0x7f8c, "rmpa.b">;
def RMPA_W : RXInst2<0x7f8d, "rmpa.w">;
def RMPA_L : RXInst2<0x7f8e, "rmpa.l">;
}
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

// stz / stnz: Rd = #imm (or Rs) if Z is set / clear.
let Uses = [PSW] in {
defm STZ  : AluFD70<0xe, "stz">;
defm STNZ : AluFD70<0xf, "stnz">;
let Predicates = [HasRXv2] in {
def STZ_rr  : AluFC<0x4b, "stz">;
def STNZ_rr : AluFC<0x4f, "stnz">;
}
}

//===----------------------------------------------------------------------===//
// Single-precision FPU
//
// The operands are IEEE single-precision values in general registers. Memory
// operands are always 32-bit ("dsp[Rs]" or "dsp[Rs].L").
//===----------------------------------------------------------------------===//

// 1111 1100 op rs rd [dsp]: op = 0x80 + 4 * sub + sd, where sd is 00 = [Rs],
// 01 = dsp8[Rs], 10 = dsp16[Rs], 11 = Rs.
class FPUrr<int op, dag outs, dag ins, string asmstr>
    : RXInst24<outs, ins, asmstr> {
  bits<4> rs;
  bits<4> rd;
  let Inst{23-16} = 0xfc;
  let Inst{15-8} = op;
  let Inst{7-4} = rs;
  let Inst{3-0} = rd;
}
class FPUm8<int op, dag outs, dag ins, string asmstr>
    : RXInst32<outs, ins, asmstr> {
  bits<4> rs;
  bits<4> rd;
  bits<8> dsp;
  let Inst{31-24} = 0xfc;
  let Inst{23-16} = op;
  let Inst{15-12} = rs;
  let Inst{11-8} = rd;
  let Inst{7-0} = dsp;
}
class FPUm16<int op, dag outs, dag ins, string asmstr>
    : RXInst40<outs, ins, asmstr> {
  bits<4> rs;
  bits<4> rd;
  bits<16> dsp;
  let Inst{39-32} = 0xfc;
  let Inst{31-24} = op;
  let Inst{23-20} = rs;
  let Inst{19-16} = rd;
  let Inst{15-8} = dsp{7-0};
  let Inst{7-0} = dsp{15-8};
}

// Register and memory source forms. `rdin` holds the inputs that come
// before the source (the tied destination of a read-modify-write
// operation, or the second operand of FCMP).
multiclass FPUSrc<int sub, string mn, dag outs, dag rdin, string cons> {
  defvar op = !add(0x80, !shl(sub, 2));
  let Constraints = cons in {
  def _rr : FPUrr<!add(op, 3), outs, !con(rdin, (ins GPR:$rs)),
                  mn#"\t$rs, $rd">;
  let mayLoad = 1 in {
  def _m0 : FPUrr<op, outs, !con(rdin, (ins GPR:$rs)), mn#"\t[$rs], $rd">;
  def _m8 : FPUm8<!add(op, 1), outs, !con(rdin, (ins GPR:$rs, dsp8l:$dsp)),
                  mn#"\t$dsp[$rs], $rd">;
  def _m16 : FPUm16<!add(op, 2), outs,
                    !con(rdin, (ins GPR:$rs, dsp16l:$dsp)),
                    mn#"\t$dsp[$rs], $rd">;
  }
  }
}

// op #imm32, Rd: 1111 1101 0111 0010 sub rd imm32
class FPUri<int sub, string mn, dag outs, dag ins, string cons>
    : RXInst56<outs, ins, mn#"\t$imm, $rd"> {
  let Constraints = cons;
  bits<4> rd;
  bits<32> imm;
  let Inst{55-48} = 0xfd;
  let Inst{47-40} = 0x72;
  let Inst{39-36} = sub;
  let Inst{35-32} = rd;
  let Inst{31-24} = imm{7-0};
  let Inst{23-16} = imm{15-8};
  let Inst{15-8} = imm{23-16};
  let Inst{7-0} = imm{31-24};
}

// Rd = Rd op src
multiclass FPUArith<int sub, string mn> {
  defm "" : FPUSrc<sub, mn, (outs GPR:$rd), (ins GPR:$src), "$src = $rd">;
  def _ri : FPUri<sub, mn, (outs GPR:$rd), (ins GPR:$src, imm32:$imm),
                  "$src = $rd">;
}

// Rd = op src
multiclass FPUConv<int sub, string mn> {
  defm "" : FPUSrc<sub, mn, (outs GPR:$rd), (ins), "">;
}

let Uses = [FPSW], Defs = [PSW, FPSW] in {
defm FSUB : FPUArith<0, "fsub">;
defm FADD : FPUArith<2, "fadd">;
defm FMUL : FPUArith<3, "fmul">;
defm FDIV : FPUArith<4, "fdiv">;

// fcmp src, Rd: sets the flags for Rd - src.
defm FCMP : FPUSrc<1, "fcmp", (outs), (ins GPR:$rd), "">;
def FCMP_ri : FPUri<1, "fcmp", (outs), (ins GPR:$rd, imm32:$imm), "">;

defm FTOI  : FPUConv<5, "ftoi">;
defm ROUND : FPUConv<6, "round">;
let Predicates = [HasRXv2] in {
defm FSQRT : FPUConv<8, "fsqrt">;
defm FTOU  : FPUConv<9, "ftou">;
}

// itof / utof Rs, Rd (integer to float). The memory forms take a memex size
// (ITOF_m* / UTOF_m*, below with the other memex sources).
def ITOF_rr : FPUrr<0x47, (outs GPR:$rd), (ins GPR:$rs), "itof\t$rs, $rd">;
let Predicates = [HasRXv2] in
def UTOF_rr : FPUrr<0x57, (outs GPR:$rd), (ins GPR:$rs), "utof\t$rs, $rd">;

// RXv2 3-operand forms: fsub computes rs2 - rs, like SUB.
let Predicates = [HasRXv2] in {
def FSUB_rrr : Alu3RR<0x8, "fsub">;
def FADD_rrr : Alu3RR<0xa, "fadd">;
def FMUL_rrr : Alu3RR<0xb, "fmul">;
}
} // Uses = [FPSW], Defs = [PSW, FPSW]

//===----------------------------------------------------------------------===//
// DSP: multiply and multiply-accumulate into an accumulator (A0 / A1)
//
// RXv1 has one accumulator and the operand is implicit; RXv2 names it.
//===----------------------------------------------------------------------===//

// op Rs, Rs2, An: 1111 1101 0g00 a op(3) rs rs2
class DSPMulBase<bits<1> g, bits<3> op, dag outs, string asmstr>
    : RXInst24<outs, (ins GPR:$rs, GPR:$rs2), asmstr> {
  bits<1> acc;
  bits<4> rs;
  bits<4> rs2;
  let Inst{23-16} = 0xfd;
  let Inst{15-8} = {0b0, g, 0b00, acc, op};
  let Inst{7-4} = rs;
  let Inst{3-0} = rs2;
}
class DSPMul<bits<1> g, bits<3> op, string mn>
    : DSPMulBase<g, op, (outs ACC:$acc), mn#"\t$rs, $rs2, $acc">;
class DSPMulV1<bits<3> op, string mn>
    : DSPMulBase<0, op, (outs), mn#"\t$rs, $rs2"> {
  let acc = 0;
}

// mvtac{hi,lo,gu} Rs, An: 1111 1101 0001 0111 a 0 op(2) rs
class MvtacBase<bits<2> op, dag outs, string asmstr>
    : RXInst24<outs, (ins GPR:$rs), asmstr> {
  bits<1> acc;
  bits<4> rs;
  let Inst{23-8} = 0xfd17;
  let Inst{7-4} = {acc, 0b0, op};
  let Inst{3-0} = rs;
}
class Mvtac<bits<2> op, string mn>
    : MvtacBase<op, (outs ACC:$acc), mn#"\t$rs, $acc">;
class MvtacV1<bits<2> op, string mn>
    : MvtacBase<op, (outs), mn#"\t$rs"> {
  let acc = 0;
}

// mvfac{hi,lo,mi,gu} #imm, An, Rd: 1111 1101 0001 111i a m op(2) rd, where
// the shift count #0, #1, #2 is encoded in (i, m) as (1, 0), (1, 1), (0, 0).
def MvfacImmClass : RXImmClass<"MvfacImm">;
def mvfacimm : Operand<i32> {
  let ParserMatchClass = MvfacImmClass;
  let EncoderMethod = "getMvfacImmOpValue";
}
class MvfacBase<bits<2> op, dag ins, string asmstr>
    : RXInst24<(outs GPR:$rd), ins, asmstr> {
  bits<2> imm;
  bits<1> acc;
  bits<4> rd;
  let Inst{23-9} = 0b111111010001111;
  let Inst{8} = imm{1};
  let Inst{7} = acc;
  let Inst{6} = imm{0};
  let Inst{5-4} = op;
  let Inst{3-0} = rd;
}
class Mvfac<bits<2> op, string mn>
    : MvfacBase<op, (ins mvfacimm:$imm, ACC:$acc), mn#"\t$imm, $acc, $rd">;
// RXv1 form: no shift, A0.
class MvfacV1<bits<2> op, string mn>
    : MvfacBase<op, (ins), mn#"\t$rd"> {
  let imm = 0b10;
  let acc = 0;
}

// rac{w,l} / rdac{w,l} #1|#2, An: 1111 1101 0001 100l a r 0 i 0000
def RacImmClass : RXImmClass<"RacImm">;
def racimm : Operand<i32> {
  let ParserMatchClass = RacImmClass;
  let EncoderMethod = "getRacImmOpValue";
}
class RacBase<bit l, bit r, dag ins, string asmstr>
    : RXInst24<(outs), ins, asmstr> {
  bits<1> imm;
  bits<1> acc;
  let Inst{23-9} = 0b111111010001100;
  let Inst{8} = l;
  let Inst{7} = acc;
  let Inst{6} = r;
  let Inst{5} = 0b0;
  let Inst{4} = imm;
  let Inst{3-0} = 0b0000;
}
class Rac<bit l, bit r, string mn>
    : RacBase<l, r, (ins racimm:$imm, ACC:$acc), mn#"\t$imm, $acc">;
class RacV1<bit l, bit r, string mn>
    : RacBase<l, r, (ins racimm:$imm), mn#"\t$imm"> {
  let acc = 0;
}

let hasSideEffects = 1 in {
def MULHI_V1 : DSPMulV1<0b000, "mulhi">;
def MULLO_V1 : DSPMulV1<0b001, "mullo">;
def MACHI_V1 : DSPMulV1<0b100, "machi">;
def MACLO_V1 : DSPMulV1<0b101, "maclo">;
def MVTACHI_V1 : MvtacV1<0b00, "mvtachi">;
def MVTACLO_V1 : MvtacV1<0b01, "mvtaclo">;
def MVFACHI_V1 : MvfacV1<0b00, "mvfachi">;
def MVFACLO_V1 : MvfacV1<0b01, "mvfaclo">;
def MVFACMI_V1 : MvfacV1<0b10, "mvfacmi">;
def RACW_V1 : RacV1<0, 0, "racw">;

let Predicates = [HasRXv2] in {
def MULHI : DSPMul<0, 0b000, "mulhi">;
def MULLO : DSPMul<0, 0b001, "mullo">;
def MULLH : DSPMul<0, 0b010, "mullh">;
def EMULA : DSPMul<0, 0b011, "emula">;
def MACHI : DSPMul<0, 0b100, "machi">;
def MACLO : DSPMul<0, 0b101, "maclo">;
def MACLH : DSPMul<0, 0b110, "maclh">;
def EMACA : DSPMul<0, 0b111, "emaca">;
def MSBHI : DSPMul<1, 0b100, "msbhi">;
def MSBLO : DSPMul<1, 0b101, "msblo">;
def MSBLH : DSPMul<1, 0b110, "msblh">;
def EMSBA : DSPMul<1, 0b111, "emsba">;
def MVTACHI : Mvtac<0b00, "mvtachi">;
def MVTACLO : Mvtac<0b01, "mvtaclo">;
def MVTACGU : Mvtac<0b11, "mvtacgu">;
def MVFACHI : Mvfac<0b00, "mvfachi">;
def MVFACLO : Mvfac<0b01, "mvfaclo">;
def MVFACMI : Mvfac<0b10, "mvfacmi">;
def MVFACGU : Mvfac<0b11, "mvfacgu">;
def RACW  : Rac<0, 0, "racw">;
def RDACW : Rac<0, 1, "rdacw">;
def RACL  : Rac<1, 0, "racl">;
def RDACL : Rac<1, 1, "rdacl">;
}
}

//===----------------------------------------------------------------------===//
// RXv2 exclusive access: movli / movco
//===----------------------------------------------------------------------===//

let Predicates = [HasRXv2], hasSideEffects = 1 in {
// movli [Rs], Rd: load Rd and set the LI flag.
let mayLoad = 1 in
def MOVLI : RXInst24<(outs GPR:$rd), (ins GPR:$rs), "movli\t[$rs], $rd"> {
  bits<4> rs;
  bits<4> rd;
  let Inst{23-8} = 0xfd2f;
  let Inst{7-4} = rs;
  let Inst{3-0} = rd;
}
// movco Rs, [Rd]: store Rs if LI is set; Rs becomes 0 on success, 1 on
// failure.
let mayStore = 1, Constraints = "$rs = $res" in
def MOVCO : RXInst24<(outs GPR:$res), (ins GPR:$rs, GPR:$rd),
                     "movco\t$rs, [$rd]"> {
  bits<4> rs;
  bits<4> rd;
  let Inst{23-8} = 0xfd27;
  let Inst{7-4} = rd;
  let Inst{3-0} = rs;
}
}

//===----------------------------------------------------------------------===//
// RXv3: bit-field move, register bank save / restore
//===----------------------------------------------------------------------===//

// bfmov / bfmovz #slsb, #dlsb, #width, Rs, Rd: copy `width` bits of Rs
// starting at bit slsb to Rd starting at bit dlsb (bfmovz clears the other
// bits of Rd). 1111 1100 0101 1z10 rs rd imm16, where imm16 (little-endian)
// is ((dlsb + width) & 31) << 10 | dlsb << 5 | ((dlsb - slsb) & 31).
def BfLsbClass   : RXImmClass<"BfLsb">;
def BfWidthClass : RXImmClass<"BfWidth">;
def bflsb : Operand<i32> { let ParserMatchClass = BfLsbClass; }
def bfwidth : Operand<i32> {
  let ParserMatchClass = BfWidthClass;
  let EncoderMethod = "getBfieldOpValue";
}
class BFMov<bits<8> op, string mn, dag outs, dag rdin, string cons>
    : RXInst40<outs, !con(rdin, (ins bflsb:$slsb, bflsb:$dlsb,
                                     bfwidth:$width, GPR:$rs)),
               mn#"\t$slsb, $dlsb, $width, $rs, $rd"> {
  let Constraints = cons;
  bits<4> rs;
  bits<4> rd;
  bits<16> width;
  let Inst{39-32} = 0xfc;
  let Inst{31-24} = op;
  let Inst{23-20} = rs;
  let Inst{19-16} = rd;
  let Inst{15-8} = width{7-0};
  let Inst{7-0} = width{15-8};
}

let Predicates = [HasRXv3] in {
def BFMOV  : BFMov<0x5e, "bfmov", (outs GPR:$rd), (ins GPR:$src),
                   "$src = $rd">;
def BFMOVZ : BFMov<0x5a, "bfmovz", (outs GPR:$rd), (ins), "">;

let hasSideEffects = 1 in {
// save / rstr Rs | #imm: 1111 1101 0111 0110 11r0 rs 0000 0000 /
// 1111 1101 0111 0110 111r 0000 imm8
class SaveRstrR<bit r, string mn>
    : RXInst32<(outs), (ins GPR:$rs), mn#"\t$rs"> {
  bits<4> rs;
  let Inst{31-16} = 0xfd76;
  let Inst{15-12} = {0b110, r};
  let Inst{11-8} = rs;
  let Inst{7-0} = 0x00;
}
class SaveRstrI<bit r, string mn>
    : RXInst32<(outs), (ins immu8:$imm), mn#"\t$imm"> {
  bits<8> imm;
  let Inst{31-16} = 0xfd76;
  let Inst{15-12} = {0b111, r};
  let Inst{11-8} = 0x0;
  let Inst{7-0} = imm;
}
def SAVE_r : SaveRstrR<0, "save">;
def SAVE_i : SaveRstrI<0, "save">;
def RSTR_r : SaveRstrR<1, "rstr">;
def RSTR_i : SaveRstrI<1, "rstr">;
}
}

//===----------------------------------------------------------------------===//
// RXv3 double-precision FPU (DFPU)
//===----------------------------------------------------------------------===//

// dsp[Rn] of dmov.d: scaled by 4, not 8 as the RXv3 manual's note says
// (an RX72M and Renesas CC-RX both scale by 4: 8[R7] is dsp 2).
def Dsp16DClass : RXDspClass<"Dsp16D">;
def Dsp8DClass  : RXDspClass<"Dsp8D", [Dsp16DClass]>;
def dsp8d  : RXDsp<"Dsp8D", 2>;
def dsp16d : RXDsp<"Dsp16D", 2>;

// The second register of a DRm-DRn / DCRm-DCRn range, encoded as n - m.
def dprhi : RegisterOperand<DPR> { let EncoderMethod = "getRegRangeOpValue"; }
def dcrhi : RegisterOperand<DCR> { let EncoderMethod = "getRegRangeOpValue"; }

// op DRs, DRs2, DRd: 0111 0110 1001 0000 rs2 op rd rs
class DFPU3<bits<4> op, string mn>
    : RXInst32<(outs DPR:$rd), (ins DPR:$rs, DPR:$rs2),
               mn#"\t$rs, $rs2, $rd"> {
  bits<4> rs;
  bits<4> rs2;
  bits<4> rd;
  let Inst{31-16} = 0x7690;
  let Inst{15-12} = rs2;
  let Inst{11-8} = op;
  let Inst{7-4} = rd;
  let Inst{3-0} = rs;
}

// op DRs, DRd: 0111 0110 1001 0000 rs sub rd sub2
class DFPU2<bits<4> sub, bits<4> sub2, string mn>
    : RXInst32<(outs DPR:$rd), (ins DPR:$rs), mn#"\t$rs, $rd"> {
  bits<4> rs;
  bits<4> rd;
  let Inst{31-16} = 0x7690;
  let Inst{15-12} = rs;
  let Inst{11-8} = sub;
  let Inst{7-4} = rd;
  let Inst{3-0} = sub2;
}

// Transfers between a general register and a DFPU register:
// 1111 1101 0111 01d1 1000 gr dr op   (d = 1: to the DFPU register)
// The general register is always in bits 11-8 (RXv3 manual, section 4).
// GNU as 2.43 swaps the two fields for ftod / itod / utod / mvtdc, so for
// those the assembler tests check the manual's bytes instead of GNU as.
class DFPUXfer<bit d, bits<4> op, dag outs, dag ins, string asmstr>
    : RXInst32<outs, ins, asmstr> {
  bits<4> gr;
  bits<4> dr;
  let Inst{31-24} = 0xfd;
  let Inst{23-16} = {0b011101, d, 0b1};
  let Inst{15-12} = 0x8;
  let Inst{11-8} = gr;
  let Inst{7-4} = dr;
  let Inst{3-0} = op;
}

// dmov.d DRs, [Rb] / dsp[Rb]: 1111 1100 0111 10sd rb 1000 [dsp] dr 0000
// dmov.d [Rb] / dsp[Rb], DRd: 1111 1100 1100 10sd rb 1000 [dsp] dr 0000
class DMovMem0<bits<8> op, dag outs, dag ins, string asmstr>
    : RXInst32<outs, ins, asmstr> {
  bits<4> rb;
  bits<4> dr;
  let Inst{31-24} = 0xfc;
  let Inst{23-16} = op;
  let Inst{15-12} = rb;
  let Inst{11-8} = 0x8;
  let Inst{7-4} = dr;
  let Inst{3-0} = 0x0;
}
class DMovMem8<bits<8> op, dag outs, dag ins, string asmstr>
    : RXInst40<outs, ins, asmstr> {
  bits<4> rb;
  bits<4> dr;
  bits<8> dsp;
  let Inst{39-32} = 0xfc;
  let Inst{31-24} = op;
  let Inst{23-20} = rb;
  let Inst{19-16} = 0x8;
  let Inst{15-8} = dsp;
  let Inst{7-4} = dr;
  let Inst{3-0} = 0x0;
}
class DMovMem16<bits<8> op, dag outs, dag ins, string asmstr>
    : RXInst48<outs, ins, asmstr> {
  bits<4> rb;
  bits<4> dr;
  bits<16> dsp;
  let Inst{47-40} = 0xfc;
  let Inst{39-32} = op;
  let Inst{31-28} = rb;
  let Inst{27-24} = 0x8;
  let Inst{23-16} = dsp{7-0};
  let Inst{15-8} = dsp{15-8};
  let Inst{7-4} = dr;
  let Inst{3-0} = 0x0;
}

// dmov.{d,l} #imm32, DRHn / DRLn: 1111 1001 0000 0011 dr op imm32
class DMovImm<bits<4> op, string mn, RegisterClass rc>
    : RXInst56<(outs rc:$dr), (ins imm32:$imm), mn#"\t$imm, $dr"> {
  bits<4> dr;
  bits<32> imm;
  let Inst{55-40} = 0xf903;
  let Inst{39-36} = dr;
  let Inst{35-32} = op;
  let Inst{31-24} = imm{7-0};
  let Inst{23-16} = imm{15-8};
  let Inst{15-8} = imm{23-16};
  let Inst{7-0} = imm{31-24};
}

// dpushm / dpopm: 0111 0101 1 0 kind(1) 1 pop(1) 000 lo count
class DPushPop<bits<8> op, string mn, RegisterClass rc, RegisterOperand hi>
    : RXInst24<(outs), (ins rc:$lo, hi:$hi), mn#"\t$lo-$hi"> {
  bits<4> lo;
  bits<4> hi;
  let Inst{23-16} = 0x75;
  let Inst{15-8} = op;
  let Inst{7-4} = lo;
  let Inst{3-0} = hi;
}

// Condition of dcmp<cond>.
class RXDCC<string n, int v> {
  string Name = n;
  int Value = v;
}

let Predicates = [HasDFPU], hasSideEffects = 1 in {
def DADD : DFPU3<0x0, "dadd">;
def DSUB : DFPU3<0x1, "dsub">;
def DMUL : DFPU3<0x2, "dmul">;
def DDIV : DFPU3<0x5, "ddiv">;

def DMOV_D_rr : DFPU2<0xc, 0x0, "dmov.d">;
def DABS   : DFPU2<0xc, 0x1, "dabs">;
def DNEG   : DFPU2<0xc, 0x2, "dneg">;
def DSQRT  : DFPU2<0xd, 0x0, "dsqrt">;
def DTOI   : DFPU2<0xd, 0x8, "dtoi">;
def DTOU   : DFPU2<0xd, 0x9, "dtou">;
def DTOF   : DFPU2<0xd, 0xc, "dtof">;
def DROUND : DFPU2<0xd, 0xd, "dround">;

// dcmp<cond> DRs, DRs2: 0111 0110 1001 0000 rs2 1000 cond rs
foreach C = [RXDCC<"un", 1>, RXDCC<"eq", 2>, RXDCC<"lt", 4>,
             RXDCC<"le", 6>] in
  def DCMP#!toupper(C.Name)
      : RXInst32<(outs), (ins DPR:$rs, DPR:$rs2), "dcmp"#C.Name#"\t$rs, $rs2"> {
    bits<4> rs;
    bits<4> rs2;
    let Inst{31-16} = 0x7690;
    let Inst{15-12} = rs2;
    let Inst{11-8} = 0x8;
    let Inst{7-4} = C.Value;
    let Inst{3-0} = rs;
  }

// Integer / single-precision to double: ftod / itod / utod Rs, DRd.
def FTOD : DFPUXfer<1, 0xa, (outs DPR:$dr), (ins GPR:$gr), "ftod\t$gr, $dr">;
def ITOD : DFPUXfer<1, 0x9, (outs DPR:$dr), (ins GPR:$gr), "itod\t$gr, $dr">;
def UTOD : DFPUXfer<1, 0xd, (outs DPR:$dr), (ins GPR:$gr), "utod\t$gr, $dr">;

// dmov.d Rs, DRHn sets DRn to Rs:0; dmov.l writes or reads one half.
def DMOV_D_rh : DFPUXfer<1, 0x3, (outs DRH:$dr), (ins GPR:$gr),
                         "dmov.d\t$gr, $dr">;
def DMOV_L_rh : DFPUXfer<1, 0x2, (outs DRH:$dr), (ins GPR:$gr),
                         "dmov.l\t$gr, $dr">;
def DMOV_L_rl : DFPUXfer<1, 0x0, (outs DRL:$dr), (ins GPR:$gr),
                         "dmov.l\t$gr, $dr">;
def DMOV_L_hr : DFPUXfer<0, 0x2, (outs GPR:$gr), (ins DRH:$dr),
                         "dmov.l\t$dr, $gr">;
def DMOV_L_lr : DFPUXfer<0, 0x0, (outs GPR:$gr), (ins DRL:$dr),
                         "dmov.l\t$dr, $gr">;

def MVTDC : DFPUXfer<1, 0x4, (outs DCR:$dr), (ins GPR:$gr), "mvtdc\t$gr, $dr">;
def MVFDC : DFPUXfer<0, 0x4, (outs GPR:$gr), (ins DCR:$dr), "mvfdc\t$dr, $gr">;
def MVFDR : RXInst24<(outs), (ins), "mvfdr"> {
  let Inst = 0x75901b;
}

let mayStore = 1 in {
def DMOV_D_st0 : DMovMem0<0x78, (outs), (ins DPR:$dr, GPR:$rb),
                          "dmov.d\t$dr, [$rb]">;
def DMOV_D_st8 : DMovMem8<0x79, (outs), (ins DPR:$dr, GPR:$rb, dsp8d:$dsp),
                          "dmov.d\t$dr, $dsp[$rb]">;
def DMOV_D_st16 : DMovMem16<0x7a, (outs),
                            (ins DPR:$dr, GPR:$rb, dsp16d:$dsp),
                            "dmov.d\t$dr, $dsp[$rb]">;
}
let mayLoad = 1 in {
def DMOV_D_ld0 : DMovMem0<0xc8, (outs DPR:$dr), (ins GPR:$rb),
                          "dmov.d\t[$rb], $dr">;
def DMOV_D_ld8 : DMovMem8<0xc9, (outs DPR:$dr), (ins GPR:$rb, dsp8d:$dsp),
                          "dmov.d\t$dsp[$rb], $dr">;
def DMOV_D_ld16 : DMovMem16<0xca, (outs DPR:$dr), (ins GPR:$rb, dsp16d:$dsp),
                            "dmov.d\t$dsp[$rb], $dr">;
}

def DMOV_D_ih : DMovImm<0x3, "dmov.d", DRH>;
def DMOV_L_ih : DMovImm<0x2, "dmov.l", DRH>;
def DMOV_L_il : DMovImm<0x0, "dmov.l", DRL>;

let Uses = [R0], Defs = [R0] in {
let mayStore = 1 in {
def DPUSHM_D : DPushPop<0xb0, "dpushm.d", DPR, dprhi>;
def DPUSHM_L : DPushPop<0xa0, "dpushm.l", DCR, dcrhi>;
}
let mayLoad = 1 in {
def DPOPM_D : DPushPop<0xb8, "dpopm.d", DPR, dprhi>;
def DPOPM_L : DPushPop<0xa8, "dpopm.l", DCR, dcrhi>;
}
}
} // Predicates = [HasDFPU]

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
def RXfcmp     : SDNode<"RXISD::FCMP", SDT_RXCmp, [SDNPOutGlue]>;
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
                      [(set (i32 GPR:$rd),
                            (RXselectcc (i32 GPR:$t), (i32 GPR:$f),
                                        imm:$cc))]>;

// Conditional branch on a condition code; lowered to b<cond>.b. Branch
// relaxation inverts it around a bra.a when the target is out of range.
let isBranch = 1, isTerminator = 1, Uses = [PSW], Size = 2 in
def BCC : RXPseudo<(outs), (ins brtarget8:$dst, i32imm:$cc),
                   [(RXbrcc bb:$dst, imm:$cc)]>;

// "bra label", "bsr label" and "b<cond> label" without a size (GNU as
// syntax): the assembler picks the form from the distance. $kind is 0 for
// bra, 1 for bsr and 2 + cond for b<cond>; $step selects the form (see
// MCTargetDesc/RXSizelessBranch.h). Only made by the assembly parser.
let isBranch = 1, Size = 6 in
def BR_SIZELESS : RXPseudo<(outs), (ins brtarget8:$dst, i32imm:$kind,
                                        i32imm:$step)>;

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
                    [(store (i32 GPR:$v), AddrL:$a)]>;
def ST_B : RXPseudo<(outs), (ins GPR:$v, mem:$a),
                    [(truncstorei8 (i32 GPR:$v), AddrB:$a)]>;
def ST_W : RXPseudo<(outs), (ins GPR:$v, mem:$a),
                    [(truncstorei16 (i32 GPR:$v), AddrW:$a)]>;
}
}
def : Pat<(i32 (extloadi8 AddrB:$a)), (LD_UB AddrB:$a)>;
def : Pat<(i32 (extloadi16 AddrW:$a)), (LD_UW AddrW:$a)>;

// Register-indexed accesses: [Ri, Rb] is Rb + Ri * size. Only for a sum of
// two registers; a constant or a stack object as one side is a dsp[Rn]
// access. AddedComplexity makes them win over a dsp[Rn] access with the
// sum as its base.
def addr_rr : PatFrag<(ops node:$a, node:$b), (add node:$a, node:$b), [{
  for (SDValue Op : N->ops())
    if (isa<ConstantSDNode>(Op) || isa<FrameIndexSDNode>(Op))
      return false;
  return true;
}]>;
def idxB : PatFrag<(ops node:$rb, node:$ri), (addr_rr node:$rb, node:$ri)>;
def idxW : PatFrag<(ops node:$rb, node:$ri),
                   (addr_rr node:$rb, (shl node:$ri, (i32 1)))>;
def idxL : PatFrag<(ops node:$rb, node:$ri),
                   (addr_rr node:$rb, (shl node:$ri, (i32 2)))>;

let AddedComplexity = 10 in {
def : Pat<(i32 (load (idxL GPR:$rb, GPR:$ri))), (MOV_L_ldx GPR:$ri, GPR:$rb)>;
def : Pat<(i32 (sextloadi16 (idxW GPR:$rb, GPR:$ri))),
          (MOV_W_ldx GPR:$ri, GPR:$rb)>;
def : Pat<(i32 (zextloadi16 (idxW GPR:$rb, GPR:$ri))),
          (MOVU_W_ldx GPR:$ri, GPR:$rb)>;
def : Pat<(i32 (extloadi16 (idxW GPR:$rb, GPR:$ri))),
          (MOVU_W_ldx GPR:$ri, GPR:$rb)>;
def : Pat<(i32 (sextloadi8 (idxB GPR:$rb, GPR:$ri))),
          (MOV_B_ldx GPR:$ri, GPR:$rb)>;
def : Pat<(i32 (zextloadi8 (idxB GPR:$rb, GPR:$ri))),
          (MOVU_B_ldx GPR:$ri, GPR:$rb)>;
def : Pat<(i32 (extloadi8 (idxB GPR:$rb, GPR:$ri))),
          (MOVU_B_ldx GPR:$ri, GPR:$rb)>;
def : Pat<(store (i32 GPR:$v), (idxL GPR:$rb, GPR:$ri)),
          (MOV_L_stx GPR:$v, GPR:$ri, GPR:$rb)>;
def : Pat<(truncstorei16 (i32 GPR:$v), (idxW GPR:$rb, GPR:$ri)),
          (MOV_W_stx GPR:$v, GPR:$ri, GPR:$rb)>;
def : Pat<(truncstorei8 (i32 GPR:$v), (idxB GPR:$rb, GPR:$ri)),
          (MOV_B_stx GPR:$v, GPR:$ri, GPR:$rb)>;
}

// Stores of a constant: mov.sz #imm, dsp[Rd]. .B always (its immediate is
// one byte); .W / .L when the stored value fits 8 bits or the constant has
// no other use (a constant stored several times is shorter loaded into a
// register once). RXMCInstLower picks the shortest encoding; Size is the
// longest.
def storeimm16 : PatLeaf<(i32 imm), [{
  uint64_t V = N->getAsZExtVal() & 0xffff;
  return N->hasOneUse() || V <= 255 || isInt<8>(int16_t(V));
}]>;
def storeimm32 : PatLeaf<(i32 imm), [{
  int64_t V = cast<ConstantSDNode>(N)->getSExtValue();
  return N->hasOneUse() || isInt<8>(V) || isUInt<8>(V);
}]>;
let mayStore = 1 in {
let Size = 5 in
def STI_B : RXPseudo<(outs), (ins mem:$a, i32imm:$imm),
                     [(truncstorei8 (i32 imm:$imm), AddrB:$a)]>;
let Size = 6 in
def STI_W : RXPseudo<(outs), (ins mem:$a, i32imm:$imm),
                     [(truncstorei16 (i32 storeimm16:$imm), AddrW:$a)]>;
let Size = 8 in
def STI_L : RXPseudo<(outs), (ins mem:$a, i32imm:$imm),
                     [(store (i32 storeimm32:$imm), AddrL:$a)]>;
}

// Naturally aligned accesses of up to 32 bits are single instructions, so
// atomic loads and stores are plain MOVs (RX parts are single-core). The
// atomic memory operands keep later passes from caching or hoisting them.
def : Pat<(i32 (atomic_load_nonext_32 AddrL:$a)), (LD_L AddrL:$a)>;
def : Pat<(i32 (atomic_load_zext_8 AddrB:$a)), (LD_UB AddrB:$a)>;
def : Pat<(i32 (atomic_load_aext_8 AddrB:$a)), (LD_UB AddrB:$a)>;
def : Pat<(i32 (atomic_load_sext_8 AddrB:$a)), (LD_B AddrB:$a)>;
def : Pat<(i32 (atomic_load_zext_16 AddrW:$a)), (LD_UW AddrW:$a)>;
def : Pat<(i32 (atomic_load_aext_16 AddrW:$a)), (LD_UW AddrW:$a)>;
def : Pat<(i32 (atomic_load_sext_16 AddrW:$a)), (LD_W AddrW:$a)>;
def : Pat<(atomic_store_32 (i32 GPR:$v), AddrL:$a), (ST_L GPR:$v, AddrL:$a)>;
def : Pat<(atomic_store_16 (i32 GPR:$v), AddrW:$a), (ST_W GPR:$v, AddrW:$a)>;
def : Pat<(atomic_store_8 (i32 GPR:$v), AddrB:$a), (ST_B GPR:$v, AddrB:$a)>;

// 32-bit compare-and-swap: $dst = [$ptr]; if $dst == $cmp, [$ptr] = $new.
// RXAsmPrinter expands it after register allocation (so that nothing is
// spilled inside the sequence) into a MOVLI/MOVCO loop on RXv2 and later,
// or into a load / compare / store with interrupts masked on RXv1. $tmp
// holds the MOVCO result or the saved PSW. Both expansions are 16 bytes.
let mayLoad = 1, mayStore = 1, hasSideEffects = 1, Defs = [PSW], Size = 16,
    Constraints = "@earlyclobber $dst,@earlyclobber $tmp" in
def CMPXCHG32 : RXPseudo<(outs GPR:$dst, GPR:$tmp),
                         (ins GPR:$ptr, GPR:$cmp, GPR:$new)>;
def : Pat<(atomic_cmp_swap_i32 GPR:$ptr, GPR:$cmp, GPR:$new),
          (CMPXCHG32 GPR:$ptr, GPR:$cmp, GPR:$new)>;

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
def : Pat<(RXcmp (i32 GPR:$a), (i32 GPR:$b)), (CMP_rr GPR:$a, GPR:$b)>;
def : Pat<(RXcmp (i32 GPR:$a), imm:$i), (CMP_ri32 GPR:$a, imm:$i)>;

//===----------------------------------------------------------------------===//
// Single-precision float (+fpu)
//
// float values live in the general registers. Loads, stores, constants and
// bitcasts are integer moves; the FPU instructions are selected only for the
// fast-math operations that RXISelLowering leaves in the DAG (the others
// become libcalls before instruction selection).
//===----------------------------------------------------------------------===//

def fpimm_bits : SDNodeXForm<fpimm, [{
  return CurDAG->getTargetConstant(
      N->getValueAPF().bitcastToAPInt().getZExtValue(), SDLoc(N), MVT::i32);
}]>;

def : Pat<(f32 fpimm:$i), (MOV_L_ri32 (fpimm_bits fpimm:$i))>;
def : Pat<(f32 (bitconvert (i32 GPR:$a))), (COPY_TO_REGCLASS GPR:$a, GPR)>;
def : Pat<(i32 (bitconvert (f32 GPR:$a))), (COPY_TO_REGCLASS GPR:$a, GPR)>;
def : Pat<(f32 (load AddrL:$a)), (LD_L AddrL:$a)>;
def : Pat<(store (f32 GPR:$v), AddrL:$a), (ST_L GPR:$v, AddrL:$a)>;
def : Pat<(f32 (RXselectcc (f32 GPR:$t), (f32 GPR:$f), imm:$cc)),
          (SELECT GPR:$t, GPR:$f, imm:$cc)>;

let Predicates = [HasFPU] in {
def : Pat<(fadd (f32 GPR:$a), (f32 GPR:$b)), (FADD_rr GPR:$a, GPR:$b)>;
def : Pat<(fsub (f32 GPR:$a), (f32 GPR:$b)), (FSUB_rr GPR:$a, GPR:$b)>;
def : Pat<(fmul (f32 GPR:$a), (f32 GPR:$b)), (FMUL_rr GPR:$a, GPR:$b)>;
def : Pat<(fdiv (f32 GPR:$a), (f32 GPR:$b)), (FDIV_rr GPR:$a, GPR:$b)>;
// fcmp src, src2 sets the flags for src2 - src, like cmp.
def : Pat<(RXfcmp (f32 GPR:$a), (f32 GPR:$b)), (FCMP_rr GPR:$a, GPR:$b)>;
def : Pat<(f32 (sint_to_fp (i32 GPR:$a))), (ITOF_rr GPR:$a)>;
}
let Predicates = [HasFPU, HasRXv2] in {
def : Pat<(fsqrt (f32 GPR:$a)), (FSQRT_rr GPR:$a)>;
def : Pat<(f32 (uint_to_fp (i32 GPR:$a))), (UTOF_rr GPR:$a)>;
}

//===----------------------------------------------------------------------===//
// Double precision with the DFPU (+dfpu)
//
// double stays soft-float in general register pairs (low word first, as the
// GCC ABI passes it). RXISelLowering replaces fast-math operations and
// integer-to-double conversions with the nodes below, taking and producing
// the two halves; RXAsmPrinter expands each pseudo into DMOV.L transfers to
// DR0 / DR1, the DFPU instruction and DMOV.L transfers back. No DR register
// is live outside a pseudo.
//===----------------------------------------------------------------------===//

// (lo, hi) = a op b, with op the DFPU3 operation field (dadd 0, dsub 1,
// dmul 2, ddiv 5).
def SDT_RXDArith : SDTypeProfile<2, 5, [
  SDTCisVT<0, i32>, SDTCisVT<1, i32>, SDTCisVT<2, i32>, SDTCisVT<3, i32>,
  SDTCisVT<4, i32>, SDTCisVT<5, i32>, SDTCisVT<6, i32>]>;
// (lo, hi) = op(a), for dsqrt.
def SDT_RXDUnary : SDTypeProfile<2, 2, [
  SDTCisVT<0, i32>, SDTCisVT<1, i32>, SDTCisVT<2, i32>, SDTCisVT<3, i32>]>;
// (lo, hi) = (double) a.
def SDT_RXIToD : SDTypeProfile<2, 1, [
  SDTCisVT<0, i32>, SDTCisVT<1, i32>, SDTCisVT<2, i32>]>;
// PSW.Z = x cm y, with cm the DCMPcm condition (eq 2, lt 4, le 6).
def SDT_RXDCmp : SDTypeProfile<0, 5, [
  SDTCisVT<0, i32>, SDTCisVT<1, i32>, SDTCisVT<2, i32>, SDTCisVT<3, i32>,
  SDTCisVT<4, i32>]>;

def RXdarith : SDNode<"RXISD::DARITH", SDT_RXDArith>;
def RXdsqrt  : SDNode<"RXISD::DSQRT", SDT_RXDUnary>;
def RXitod   : SDNode<"RXISD::ITOD", SDT_RXIToD>;
def RXutod   : SDNode<"RXISD::UTOD", SDT_RXIToD>;
def RXdcmp   : SDNode<"RXISD::DCMP", SDT_RXDCmp, [SDNPOutGlue]>;

let Predicates = [HasDFPU], Uses = [DPSW], Defs = [DR0, DR1, DPSW] in {
// dmov.l x4, d<op> dr1, dr0, dr0, dmov.l x2
let Size = 28 in
def DARITH_P : RXPseudo<(outs GPR:$lo, GPR:$hi),
                        (ins i32imm:$op, GPR:$alo, GPR:$ahi, GPR:$blo,
                             GPR:$bhi),
                        [(set (i32 GPR:$lo), (i32 GPR:$hi),
                              (RXdarith timm:$op, (i32 GPR:$alo),
                                        (i32 GPR:$ahi), (i32 GPR:$blo),
                                        (i32 GPR:$bhi)))]>;
// dmov.l x2, dsqrt dr0, dr0, dmov.l x2
let Size = 20 in
def DSQRT_P : RXPseudo<(outs GPR:$lo, GPR:$hi), (ins GPR:$alo, GPR:$ahi),
                       [(set (i32 GPR:$lo), (i32 GPR:$hi),
                             (RXdsqrt (i32 GPR:$alo), (i32 GPR:$ahi)))]>;
// itod / utod a, dr0, dmov.l x2
let Size = 12 in {
def ITOD_P : RXPseudo<(outs GPR:$lo, GPR:$hi), (ins GPR:$a),
                      [(set (i32 GPR:$lo), (i32 GPR:$hi),
                            (RXitod (i32 GPR:$a)))]>;
def UTOD_P : RXPseudo<(outs GPR:$lo, GPR:$hi), (ins GPR:$a),
                      [(set (i32 GPR:$lo), (i32 GPR:$hi),
                            (RXutod (i32 GPR:$a)))]>;
}
// dmov.l x4, dcmp<cm> dr1, dr0, mvfdr
let Size = 23, Defs = [DR0, DR1, DPSW, PSW] in
def DCMP_P : RXPseudo<(outs),
                      (ins i32imm:$cm, GPR:$xlo, GPR:$xhi, GPR:$ylo,
                           GPR:$yhi),
                      [(RXdcmp timm:$cm, (i32 GPR:$xlo), (i32 GPR:$xhi),
                               (i32 GPR:$ylo), (i32 GPR:$yhi))]>;
}

//===----------------------------------------------------------------------===//
// Memory operand forms: memex sources, push from memory, bit operations
//
// Each form here is: head bytes (with the ld field: 00 = [R], 01 = dsp:8[R],
// 10 = dsp:16[R]), one byte of register / immediate fields (Mid), and the
// displacement, little-endian and divided by the access size. The
// multiclasses below make the three forms _0, _8 and _16 of an instruction
// from its head for ld = 00 (Head) and the position of ld in it (LdShift);
// Asm0 and AsmD are the assembly strings without and with the displacement.
//===----------------------------------------------------------------------===//

class MemF10<dag o, dag i, string a> : RXInst16<o, i, a> {
  bits<8> Head;
  bits<8> Mid;
  let Inst{15-8} = Head;
  let Inst{7-0} = Mid;
}
class MemF11<dag o, dag i, string a> : RXInst24<o, i, a> {
  bits<8> Head;
  bits<8> Mid;
  bits<8> dsp;
  let Inst{23-16} = Head;
  let Inst{15-8} = Mid;
  let Inst{7-0} = dsp;
}
class MemF12<dag o, dag i, string a> : RXInst32<o, i, a> {
  bits<8> Head;
  bits<8> Mid;
  bits<16> dsp;
  let Inst{31-24} = Head;
  let Inst{23-16} = Mid;
  let Inst{15-8} = dsp{7-0};
  let Inst{7-0} = dsp{15-8};
}
class MemF20<dag o, dag i, string a> : RXInst24<o, i, a> {
  bits<16> Head;
  bits<8> Mid;
  let Inst{23-8} = Head;
  let Inst{7-0} = Mid;
}
class MemF21<dag o, dag i, string a> : RXInst32<o, i, a> {
  bits<16> Head;
  bits<8> Mid;
  bits<8> dsp;
  let Inst{31-16} = Head;
  let Inst{15-8} = Mid;
  let Inst{7-0} = dsp;
}
class MemF22<dag o, dag i, string a> : RXInst40<o, i, a> {
  bits<16> Head;
  bits<8> Mid;
  bits<16> dsp;
  let Inst{39-24} = Head;
  let Inst{23-16} = Mid;
  let Inst{15-8} = dsp{7-0};
  let Inst{7-0} = dsp{15-8};
}
class MemF30<dag o, dag i, string a> : RXInst32<o, i, a> {
  bits<24> Head;
  bits<8> Mid;
  let Inst{31-8} = Head;
  let Inst{7-0} = Mid;
}
class MemF31<dag o, dag i, string a> : RXInst40<o, i, a> {
  bits<24> Head;
  bits<8> Mid;
  bits<8> dsp;
  let Inst{39-16} = Head;
  let Inst{15-8} = Mid;
  let Inst{7-0} = dsp;
}
class MemF32<dag o, dag i, string a> : RXInst48<o, i, a> {
  bits<24> Head;
  bits<8> Mid;
  bits<16> dsp;
  let Inst{47-24} = Head;
  let Inst{23-16} = Mid;
  let Inst{15-8} = dsp{7-0};
  let Inst{7-0} = dsp{15-8};
}

// Mid = rs rd, with a 1-, 2- or 3-byte head.
multiclass MemRsRd1<int H, string Asm0, string AsmD, dag oops, dag iops,
                    Operand d8, Operand d16> {
  def _0 : MemF10<oops, iops, Asm0> {
    bits<4> rs;
    bits<4> rd;
    let Head = H;
    let Mid = {rs, rd};
  }
  def _8 : MemF11<oops, !con(iops, (ins d8:$dsp)), AsmD> {
    bits<4> rs;
    bits<4> rd;
    let Head = !or(H, 1);
    let Mid = {rs, rd};
  }
  def _16 : MemF12<oops, !con(iops, (ins d16:$dsp)), AsmD> {
    bits<4> rs;
    bits<4> rd;
    let Head = !or(H, 2);
    let Mid = {rs, rd};
  }
}
multiclass MemRsRd2<int H, string Asm0, string AsmD, dag oops, dag iops,
                    Operand d8, Operand d16> {
  def _0 : MemF20<oops, iops, Asm0> {
    bits<4> rs;
    bits<4> rd;
    let Head = H;
    let Mid = {rs, rd};
  }
  def _8 : MemF21<oops, !con(iops, (ins d8:$dsp)), AsmD> {
    bits<4> rs;
    bits<4> rd;
    let Head = !or(H, 1);
    let Mid = {rs, rd};
  }
  def _16 : MemF22<oops, !con(iops, (ins d16:$dsp)), AsmD> {
    bits<4> rs;
    bits<4> rd;
    let Head = !or(H, 2);
    let Mid = {rs, rd};
  }
}
// The ld field is in the middle byte of the head (06 mi 1000 ld op).
multiclass MemRsRd3<int H, string Asm0, string AsmD, dag oops, dag iops,
                    Operand d8, Operand d16> {
  def _0 : MemF30<oops, iops, Asm0> {
    bits<4> rs;
    bits<4> rd;
    let Head = H;
    let Mid = {rs, rd};
  }
  def _8 : MemF31<oops, !con(iops, (ins d8:$dsp)), AsmD> {
    bits<4> rs;
    bits<4> rd;
    let Head = !or(H, 0x100);
    let Mid = {rs, rd};
  }
  def _16 : MemF32<oops, !con(iops, (ins d16:$dsp)), AsmD> {
    bits<4> rs;
    bits<4> rd;
    let Head = !or(H, 0x200);
    let Mid = {rs, rd};
  }
}

//===----------------------------------------------------------------------===//
// op src, Rd with a memory source and a size extension (memex)
//
// memex is .B, .W, .L or .UW (06-prefixed forms with mi = 00, 01, 10, 11)
// or .UB (a shorter encoding without the 06 prefix). GNU as and the parser
// take "[Rs]" without a size extension as .L. The forms are named
// <OP>_m<memex>_0 / _8 / _16.
//===----------------------------------------------------------------------===//

class RXMemex<string n, int mi, Operand d8, Operand d16> {
  string Name = n;
  int MI = mi;
  Operand Dsp8 = d8;
  Operand Dsp16 = d16;
}
def MX_B  : RXMemex<"b",  0b00, dsp8b, dsp16b>;
def MX_W  : RXMemex<"w",  0b01, dsp8w, dsp16w>;
def MX_L  : RXMemex<"l",  0b10, dsp8l, dsp16l>;
def MX_UW : RXMemex<"uw", 0b11, dsp8w, dsp16w>;

// sub / cmp / add / mul / and / or (op = 0..5): .UB is 0100 op ld rs rd,
// the others 0000 0110 mi op ld rs rd.
multiclass MemexA<int op, string mn, dag outs, dag rdin, string cons> {
  let Constraints = cons, mayLoad = 1 in {
  defm _mUB : MemRsRd1<!or(0x40, !shl(op, 2)), mn#"\t[$rs].ub, $rd",
                       mn#"\t$dsp[$rs].ub, $rd", outs,
                       !con(rdin, (ins GPR:$rs)), dsp8b, dsp16b>;
  foreach M = [MX_B, MX_W, MX_L, MX_UW] in
  defm _m#!toupper(M.Name)
      : MemRsRd2<!or(0x0600, !shl(M.MI, 6), !shl(op, 2)),
                 mn#"\t[$rs]."#M.Name#", $rd",
                 mn#"\t$dsp[$rs]."#M.Name#", $rd", outs,
                 !con(rdin, (ins GPR:$rs)), M.Dsp8, M.Dsp16>;
  }
}

// The FC-prefixed operations (sub-operation op): .UB is 1111 1100 op ld
// rs rd, the others 0000 0110 mi 10 00 ld op rs rd. adc and sbb have only
// the .L form.
multiclass MemexB<int op, string mn, dag outs, dag rdin, string cons,
                  list<RXMemex> mxs = [MX_B, MX_W, MX_L, MX_UW],
                  bit ub = 1> {
  let Constraints = cons, mayLoad = 1 in {
  if ub then
  defm _mUB : MemRsRd2<!or(0xfc00, !shl(op, 2)), mn#"\t[$rs].ub, $rd",
                       mn#"\t$dsp[$rs].ub, $rd", outs,
                       !con(rdin, (ins GPR:$rs)), dsp8b, dsp16b>;
  foreach M = mxs in
  defm _m#!toupper(M.Name)
      : MemRsRd3<!or(0x062000, !shl(M.MI, 14), op),
                 mn#"\t[$rs]."#M.Name#", $rd",
                 mn#"\t$dsp[$rs]."#M.Name#", $rd", outs,
                 !con(rdin, (ins GPR:$rs)), M.Dsp8, M.Dsp16>;
  }
}

let Defs = [PSW] in {
defm SUB : MemexA<0, "sub", (outs GPR:$rd), (ins GPR:$src), "$src = $rd">;
defm CMP : MemexA<1, "cmp", (outs), (ins GPR:$rd), "">;
defm ADD : MemexA<2, "add", (outs GPR:$rd), (ins GPR:$src), "$src = $rd">;
defm MUL : MemexA<3, "mul", (outs GPR:$rd), (ins GPR:$src), "$src = $rd">;
defm AND : MemexA<4, "and", (outs GPR:$rd), (ins GPR:$src), "$src = $rd">;
defm OR  : MemexA<5, "or",  (outs GPR:$rd), (ins GPR:$src), "$src = $rd">;

let Uses = [PSW] in {
defm SBB : MemexB<0x00, "sbb", (outs GPR:$rd), (ins GPR:$src), "$src = $rd",
                  [MX_L], 0>;
defm ADC : MemexB<0x02, "adc", (outs GPR:$rd), (ins GPR:$src), "$src = $rd",
                  [MX_L], 0>;
}
defm MAX   : MemexB<0x04, "max", (outs GPR:$rd), (ins GPR:$src),
                    "$src = $rd">;
defm MIN   : MemexB<0x05, "min", (outs GPR:$rd), (ins GPR:$src),
                    "$src = $rd">;
defm EMUL  : MemexB<0x06, "emul", (outs GPR:$rd), (ins GPR:$src),
                    "$src = $rd">;
defm EMULU : MemexB<0x07, "emulu", (outs GPR:$rd), (ins GPR:$src),
                    "$src = $rd">;
defm DIV   : MemexB<0x08, "div", (outs GPR:$rd), (ins GPR:$src),
                    "$src = $rd">;
defm DIVU  : MemexB<0x09, "divu", (outs GPR:$rd), (ins GPR:$src),
                    "$src = $rd">;
defm TST   : MemexB<0x0c, "tst", (outs), (ins GPR:$rd), "">;
defm XOR   : MemexB<0x0d, "xor", (outs GPR:$rd), (ins GPR:$src),
                    "$src = $rd">;
}

// xchg: Rs (or the memory operand) and Rd swap.
def XCHG_rr : RXInst24<(outs GPR:$rd, GPR:$rs_wb), (ins GPR:$src, GPR:$rs),
                       "xchg\t$rs, $rd"> {
  let Constraints = "$src = $rd, $rs = $rs_wb";
  bits<4> rs;
  bits<4> rd;
  let Inst{23-16} = 0xfc;
  let Inst{15-8} = 0x43;
  let Inst{7-4} = rs;
  let Inst{3-0} = rd;
}
let mayStore = 1 in
defm XCHG : MemexB<0x10, "xchg", (outs GPR:$rd), (ins GPR:$src),
                   "$src = $rd">;

// itof / utof src, Rd
let Uses = [FPSW], Defs = [PSW, FPSW] in {
defm ITOF : MemexB<0x11, "itof", (outs GPR:$rd), (ins), "">;
let Predicates = [HasRXv2] in
defm UTOF : MemexB<0x15, "utof", (outs GPR:$rd), (ins), "">;
}

//===----------------------------------------------------------------------===//
// push.sz src with a memory source: 1111 01ld rs 10 sz [dsp]
//===----------------------------------------------------------------------===//

multiclass PushM<int sz, string sfx, Operand d8, Operand d16> {
  defvar mn = "push."#sfx;
  def _0 : MemF10<(outs), (ins GPR:$rs), mn#"\t[$rs]"> {
    bits<4> rs;
    let Head = 0xf4;
    let Mid{7-4} = rs;
    let Mid{3-0} = !or(8, sz);
  }
  def _8 : MemF11<(outs), (ins GPR:$rs, d8:$dsp), mn#"\t$dsp[$rs]"> {
    bits<4> rs;
    let Head = 0xf5;
    let Mid{7-4} = rs;
    let Mid{3-0} = !or(8, sz);
  }
  def _16 : MemF12<(outs), (ins GPR:$rs, d16:$dsp), mn#"\t$dsp[$rs]"> {
    bits<4> rs;
    let Head = 0xf6;
    let Mid{7-4} = rs;
    let Mid{3-0} = !or(8, sz);
  }
}
let Uses = [R0], Defs = [R0], mayLoad = 1, mayStore = 1 in {
defm PUSH_B_m : PushM<0b00, "b", dsp8b, dsp16b>;
defm PUSH_W_m : PushM<0b01, "w", dsp8w, dsp16w>;
defm PUSH_L_m : PushM<0b10, "l", dsp8l, dsp16l>;
}

//===----------------------------------------------------------------------===//
// Bit operations
//
// The memory operand is a byte, written "[Rd].B" (GNU as requires the .B).
// bset / bclr / btst #imm3, mem: 1111 0b ld rd c imm3 [dsp] (b = 1 for btst,
// c = 1 for bclr). bset / bclr / btst / bnot Rs, mem / Rd: 1111 1100 0110
// op ld rd rs [dsp] (ld = 11 for Rd). bnot / bm<cond> #imm3, mem: 1111 1100
// 111 imm3 ld rd cond [dsp] (cond = 1111 for bnot). bset / bclr / btst
// #imm5, Rd: 0111 1 op imm5 rd; bnot / bm<cond> #imm5, Rd: 1111 1101 111
// imm5 cond rd.
//===----------------------------------------------------------------------===//

def ImmU3Class : RXImmClass<"ImmU3">;
def immu3 : Operand<i32> { let ParserMatchClass = ImmU3Class; }

multiclass BitImmMem<int b, int c, string mn, dag oops, dag iops> {
  def _im0 : MemF10<oops, !con(iops, (ins immu3:$imm, GPR:$rd)),
                    mn#"\t$imm, [$rd].b"> {
    bits<4> rd;
    bits<3> imm;
    let Head = !or(0xf0, !shl(b, 2));
    let Mid{7-4} = rd;
    let Mid{3} = c;
    let Mid{2-0} = imm;
  }
  def _im8 : MemF11<oops, !con(iops, (ins immu3:$imm, GPR:$rd, dsp8b:$dsp)),
                    mn#"\t$imm, $dsp[$rd].b"> {
    bits<4> rd;
    bits<3> imm;
    let Head = !or(0xf1, !shl(b, 2));
    let Mid{7-4} = rd;
    let Mid{3} = c;
    let Mid{2-0} = imm;
  }
  def _im16 : MemF12<oops,
                     !con(iops, (ins immu3:$imm, GPR:$rd, dsp16b:$dsp)),
                     mn#"\t$imm, $dsp[$rd].b"> {
    bits<4> rd;
    bits<3> imm;
    let Head = !or(0xf2, !shl(b, 2));
    let Mid{7-4} = rd;
    let Mid{3} = c;
    let Mid{2-0} = imm;
  }
}

// bnot / bm<cond> #imm3, mem (the condition in the low nibble of Mid).
multiclass BitCondMem<int cond, string mn> {
  def _im0 : MemF20<(outs), (ins immu3:$imm, GPR:$rd), mn#"\t$imm, [$rd].b"> {
    bits<4> rd;
    bits<3> imm;
    let Head{15-5} = 0b11111100111;
    let Head{4-2} = imm;
    let Head{1-0} = 0b00;
    let Mid{7-4} = rd;
    let Mid{3-0} = cond;
  }
  def _im8 : MemF21<(outs), (ins immu3:$imm, GPR:$rd, dsp8b:$dsp),
                    mn#"\t$imm, $dsp[$rd].b"> {
    bits<4> rd;
    bits<3> imm;
    let Head{15-5} = 0b11111100111;
    let Head{4-2} = imm;
    let Head{1-0} = 0b01;
    let Mid{7-4} = rd;
    let Mid{3-0} = cond;
  }
  def _im16 : MemF22<(outs), (ins immu3:$imm, GPR:$rd, dsp16b:$dsp),
                     mn#"\t$imm, $dsp[$rd].b"> {
    bits<4> rd;
    bits<3> imm;
    let Head{15-5} = 0b11111100111;
    let Head{4-2} = imm;
    let Head{1-0} = 0b10;
    let Mid{7-4} = rd;
    let Mid{3-0} = cond;
  }
}

// bset / bclr / btst / bnot Rs, mem / Rd (op = 0..3)
multiclass BitRegMem<int op, string mn, dag oops, dag rdin, string cons> {
  def _rm0 : MemF20<(outs), (ins GPR:$rs, GPR:$rd), mn#"\t$rs, [$rd].b"> {
    bits<4> rs;
    bits<4> rd;
    let Head = !or(0xfc60, !shl(op, 2));
    let Mid = {rd, rs};
  }
  def _rm8 : MemF21<(outs), (ins GPR:$rs, GPR:$rd, dsp8b:$dsp),
                    mn#"\t$rs, $dsp[$rd].b"> {
    bits<4> rs;
    bits<4> rd;
    let Head = !or(0xfc61, !shl(op, 2));
    let Mid = {rd, rs};
  }
  def _rm16 : MemF22<(outs), (ins GPR:$rs, GPR:$rd, dsp16b:$dsp),
                     mn#"\t$rs, $dsp[$rd].b"> {
    bits<4> rs;
    bits<4> rd;
    let Head = !or(0xfc62, !shl(op, 2));
    let Mid = {rd, rs};
  }
  def _rr : MemF20<oops, !con(rdin, (ins GPR:$rs)), mn#"\t$rs, $rd"> {
    let Constraints = cons;
    bits<4> rs;
    bits<4> rd;
    let Head = !or(0xfc63, !shl(op, 2));
    let Mid = {rd, rs};
  }
}

// bset / bclr / btst #imm5, Rd: 0111 1 op imm5 rd
class BitImmReg<int op, string mn, dag oops, dag iops, string cons>
    : RXInst16<oops, !con(iops, (ins immu5:$imm)), mn#"\t$imm, $rd"> {
  let Constraints = cons;
  bits<4> rd;
  bits<5> imm;
  let Inst{15-11} = 0b01111;
  let Inst{10-9} = op;
  let Inst{8-4} = imm;
  let Inst{3-0} = rd;
}

// bnot / bm<cond> #imm5, Rd: 1111 1101 111 imm5 cond rd
class BitCondReg<int cond, string mn, list<Register> uses>
    : RXInst24<(outs GPR:$rd), (ins GPR:$src, immu5:$imm), mn#"\t$imm, $rd"> {
  let Constraints = "$src = $rd";
  let Uses = uses;
  bits<4> rd;
  bits<5> imm;
  let Inst{23-16} = 0xfd;
  let Inst{15-13} = 0b111;
  let Inst{12-8} = imm;
  let Inst{7-4} = cond;
  let Inst{3-0} = rd;
}

let mayLoad = 1, mayStore = 1 in {
defm BSET : BitImmMem<0, 0, "bset", (outs), (ins)>;
defm BCLR : BitImmMem<0, 1, "bclr", (outs), (ins)>;
defm BNOT : BitCondMem<0xf, "bnot">;
let Uses = [PSW] in
foreach C = [CC_EQ, CC_NE, CC_GEU, CC_LTU, CC_GTU, CC_LEU, CC_PZ, CC_N,
             CC_GE, CC_LT, CC_GT, CC_LE, CC_O, CC_NO] in
defm BM#!toupper(C.Name) : BitCondMem<C.Value, "bm"#C.Name>;
}
let mayLoad = 1, Defs = [PSW] in
defm BTST : BitImmMem<1, 0, "btst", (outs), (ins)>;

let mayLoad = 1, mayStore = 1 in {
defm BSET : BitRegMem<0, "bset", (outs GPR:$rd), (ins GPR:$src),
                      "$src = $rd">;
defm BCLR : BitRegMem<1, "bclr", (outs GPR:$rd), (ins GPR:$src),
                      "$src = $rd">;
defm BNOT : BitRegMem<3, "bnot", (outs GPR:$rd), (ins GPR:$src),
                      "$src = $rd">;
}
let mayLoad = 1, Defs = [PSW] in
defm BTST : BitRegMem<2, "btst", (outs), (ins GPR:$rd), "">;

def BSET_ir : BitImmReg<0, "bset", (outs GPR:$rd), (ins GPR:$src),
                        "$src = $rd">;
def BCLR_ir : BitImmReg<1, "bclr", (outs GPR:$rd), (ins GPR:$src),
                        "$src = $rd">;
let Defs = [PSW] in
def BTST_ir : BitImmReg<2, "btst", (outs), (ins GPR:$rd), "">;
def BNOT_ir : BitCondReg<0xf, "bnot", []>;
foreach C = [CC_EQ, CC_NE, CC_GEU, CC_LTU, CC_GTU, CC_LEU, CC_PZ, CC_N,
             CC_GE, CC_LT, CC_GT, CC_LE, CC_O, CC_NO] in
def BM#!toupper(C.Name)#_ir : BitCondReg<C.Value, "bm"#C.Name, [PSW]>;

// Alternative condition names accepted by GNU as.
foreach A = [["z", "BMEQ"], ["nz", "BMNE"], ["c", "BMGEU"], ["nc", "BMLTU"]] in {
def : InstAlias<"bm"#A[0]#"\t$imm, $rd",
                (!cast<Instruction>(A[1]#"_ir") GPR:$rd, immu5:$imm), 0>;
def : InstAlias<"bm"#A[0]#"\t$imm, [$rd].b",
                (!cast<Instruction>(A[1]#"_im0") immu3:$imm, GPR:$rd), 0>;
def : InstAlias<"bm"#A[0]#"\t$imm, $dsp[$rd].b",
                (!cast<Instruction>(A[1]#"_im8") immu3:$imm, GPR:$rd,
                 dsp8b:$dsp), 0>;
def : InstAlias<"bm"#A[0]#"\t$imm, $dsp[$rd].b",
                (!cast<Instruction>(A[1]#"_im16") immu3:$imm, GPR:$rd,
                 dsp16b:$dsp), 0>;
}
