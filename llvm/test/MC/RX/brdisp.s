# RUN: llvm-mc -triple=rx-none-elf -mcpu=rxv1 -show-encoding %s \
# RUN:   | FileCheck %s
# RUN: llvm-mc -triple=rx-none-elf -mcpu=rxv3e -show-encoding %s \
# RUN:   | FileCheck %s

# Branches with a constant displacement (bytes from the start of the
# branch), the form the disassembler prints;
# not compared with GNU as (GNU as reads a constant as an address).
bra.s 3
# CHECK: bra.s 3 ; encoding: [0x0b]
bra.s 7
# CHECK: bra.s 7 ; encoding: [0x0f]
bra.s 8
# CHECK: bra.s 8 ; encoding: [0x08]
bra.s 10
# CHECK: bra.s 10 ; encoding: [0x0a]
beq.s 3
# CHECK: beq.s 3 ; encoding: [0x13]
beq.s 10
# CHECK: beq.s 10 ; encoding: [0x12]
bne.s 5
# CHECK: bne.s 5 ; encoding: [0x1d]
bne.s 9
# CHECK: bne.s 9 ; encoding: [0x19]
bra.b -128
# CHECK: bra.b -128 ; encoding: [0x2e,0x80]
bra.b 0
# CHECK: bra.b 0 ; encoding: [0x2e,0x00]
bra.b 127
# CHECK: bra.b 127 ; encoding: [0x2e,0x7f]
beq.b -2
# CHECK: beq.b -2 ; encoding: [0x20,0xfe]
bne.b 100
# CHECK: bne.b 100 ; encoding: [0x21,0x64]
bgeu.b -50
# CHECK: bgeu.b -50 ; encoding: [0x22,0xce]
bltu.b 50
# CHECK: bltu.b 50 ; encoding: [0x23,0x32]
bgtu.b 1
# CHECK: bgtu.b 1 ; encoding: [0x24,0x01]
bleu.b -1
# CHECK: bleu.b -1 ; encoding: [0x25,0xff]
bpz.b 64
# CHECK: bpz.b 64 ; encoding: [0x26,0x40]
bn.b -64
# CHECK: bn.b -64 ; encoding: [0x27,0xc0]
bge.b 20
# CHECK: bge.b 20 ; encoding: [0x28,0x14]
blt.b -20
# CHECK: blt.b -20 ; encoding: [0x29,0xec]
bgt.b 30
# CHECK: bgt.b 30 ; encoding: [0x2a,0x1e]
ble.b -30
# CHECK: ble.b -30 ; encoding: [0x2b,0xe2]
bo.b 4
# CHECK: bo.b 4 ; encoding: [0x2c,0x04]
bno.b -4
# CHECK: bno.b -4 ; encoding: [0x2d,0xfc]
bra.w -32768
# CHECK: bra.w -32768 ; encoding: [0x38,0x00,0x80]
bra.w 32767
# CHECK: bra.w 32767 ; encoding: [0x38,0xff,0x7f]
beq.w -300
# CHECK: beq.w -300 ; encoding: [0x3a,0xd4,0xfe]
bne.w 300
# CHECK: bne.w 300 ; encoding: [0x3b,0x2c,0x01]
bsr.w 1000
# CHECK: bsr.w 1000 ; encoding: [0x39,0xe8,0x03]
bsr.w -1000
# CHECK: bsr.w -1000 ; encoding: [0x39,0x18,0xfc]
bra.a -8388608
# CHECK: bra.a -8388608 ; encoding: [0x04,0x00,0x00,0x80]
bra.a 8388607
# CHECK: bra.a 8388607 ; encoding: [0x04,0xff,0xff,0x7f]
bsr.a 65536
# CHECK: bsr.a 65536 ; encoding: [0x05,0x00,0x00,0x01]
bsr.a -65536
# CHECK: bsr.a -65536 ; encoding: [0x05,0x00,0x00,0xff]
