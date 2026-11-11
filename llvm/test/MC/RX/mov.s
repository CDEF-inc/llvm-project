# RUN: llvm-mc -triple=rx-none-elf -mcpu=rxv1 -show-encoding %s \
# RUN:   | FileCheck %s
# RUN: llvm-mc -triple=rx-none-elf -mcpu=rxv3e -show-encoding %s \
# RUN:   | FileCheck %s

# MOV / MOVU addressing forms beyond Rn, #imm and dsp[Rn]. One instruction
# per line. The encodings also agree
# with Renesas CC-RX V3.08, except where GNU as picks a shorter immediate
# field for the same value (mov.w #0xffff is #-1 in 8 bits).
# The encodings match GNU as 2.43 (rx-elf).

# --- post-increment / pre-decrement
mov.b r1, [r2+]
# CHECK: mov.b r1, [r2+] ; encoding: [0xfd,0x20,0x21]
mov.w r1, [r2+]
# CHECK: mov.w r1, [r2+] ; encoding: [0xfd,0x21,0x21]
mov.l r1, [r2+]
# CHECK: mov.l r1, [r2+] ; encoding: [0xfd,0x22,0x21]
mov.b r0, [r15+]
# CHECK: mov.b r0, [r15+] ; encoding: [0xfd,0x20,0xf0]
mov.b r1, [-r2]
# CHECK: mov.b r1, [-r2] ; encoding: [0xfd,0x24,0x21]
mov.w r1, [-r2]
# CHECK: mov.w r1, [-r2] ; encoding: [0xfd,0x25,0x21]
mov.l r1, [-r2]
# CHECK: mov.l r1, [-r2] ; encoding: [0xfd,0x26,0x21]
mov.b [r2+], r1
# CHECK: mov.b [r2+], r1 ; encoding: [0xfd,0x28,0x21]
mov.w [r2+], r1
# CHECK: mov.w [r2+], r1 ; encoding: [0xfd,0x29,0x21]
mov.l [r2+], r1
# CHECK: mov.l [r2+], r1 ; encoding: [0xfd,0x2a,0x21]
mov.l [r1+], r2
# CHECK: mov.l [r1+], r2 ; encoding: [0xfd,0x2a,0x12]
mov.b [-r2], r1
# CHECK: mov.b [-r2], r1 ; encoding: [0xfd,0x2c,0x21]
mov.w [-r2], r1
# CHECK: mov.w [-r2], r1 ; encoding: [0xfd,0x2d,0x21]
mov.l [-r2], r1
# CHECK: mov.l [-r2], r1 ; encoding: [0xfd,0x2e,0x21]
movu.b [r2+], r1
# CHECK: movu.b [r2+], r1 ; encoding: [0xfd,0x38,0x21]
movu.w [r2+], r1
# CHECK: movu.w [r2+], r1 ; encoding: [0xfd,0x39,0x21]
movu.b [-r2], r1
# CHECK: movu.b [-r2], r1 ; encoding: [0xfd,0x3c,0x21]
movu.w [-r2], r1
# CHECK: movu.w [-r2], r1 ; encoding: [0xfd,0x3d,0x21]

# --- register-indexed: [Ri, Rb] is Rb + Ri * size
mov.b [r3, r4], r5
# CHECK: mov.b [r3, r4], r5 ; encoding: [0xfe,0x43,0x45]
mov.w [r3, r4], r5
# CHECK: mov.w [r3, r4], r5 ; encoding: [0xfe,0x53,0x45]
mov.l [r3, r4], r5
# CHECK: mov.l [r3, r4], r5 ; encoding: [0xfe,0x63,0x45]
mov.l [r10, r11], r12
# CHECK: mov.l [r10, r11], r12 ; encoding: [0xfe,0x6a,0xbc]
mov.l [r0, r0], r0
# CHECK: mov.l [r0, r0], r0 ; encoding: [0xfe,0x60,0x00]
mov.b r5, [r3, r4]
# CHECK: mov.b r5, [r3, r4] ; encoding: [0xfe,0x03,0x45]
mov.w r5, [r3, r4]
# CHECK: mov.w r5, [r3, r4] ; encoding: [0xfe,0x13,0x45]
mov.l r5, [r3, r4]
# CHECK: mov.l r5, [r3, r4] ; encoding: [0xfe,0x23,0x45]
movu.b [r3, r4], r5
# CHECK: movu.b [r3, r4], r5 ; encoding: [0xfe,0xc3,0x45]
movu.w [r3, r4], r5
# CHECK: movu.w [r3, r4], r5 ; encoding: [0xfe,0xd3,0x45]

# --- immediate to memory: the dsp:5 form (R0-R7, #0-255) where GNU as
# uses it, otherwise [Rd] / dsp:8 / dsp:16 with the shortest immediate
mov.b #5, 3[r2]
# CHECK: mov.b #5, 3[r2] ; encoding: [0x3c,0x23,0x05]
mov.w #5, 6[r2]
# CHECK: mov.w #5, 6[r2] ; encoding: [0x3d,0x23,0x05]
mov.l #5, 12[r2]
# CHECK: mov.l #5, 12[r2] ; encoding: [0x3e,0x23,0x05]
mov.l #5, 0[r2]
# CHECK: mov.l #5, 0[r2] ; encoding: [0x3e,0x20,0x05]
mov.b #200, 31[r7]
# CHECK: mov.b #200, 31[r7] ; encoding: [0x3c,0xff,0xc8]
mov.w #200, 62[r7]
# CHECK: mov.w #200, 62[r7] ; encoding: [0x3d,0xff,0xc8]
mov.l #200, 124[r7]
# CHECK: mov.l #200, 124[r7] ; encoding: [0x3e,0xff,0xc8]
mov.l #255, 124[r2]
# CHECK: mov.l #255, 124[r2] ; encoding: [0x3e,0xaf,0xff]
mov.w #128, 4[r2]
# CHECK: mov.w #128, 4[r2] ; encoding: [0x3d,0x22,0x80]
mov.b #200, 32[r2]
# CHECK: mov.b #200, 32[r2] ; encoding: [0xf9,0x24,0x20,0xc8]
mov.b #5, [r2]
# CHECK: mov.b #5, [r2] ; encoding: [0xf8,0x24,0x05]
mov.w #5, [r2]
# CHECK: mov.w #5, [r2] ; encoding: [0xf8,0x25,0x05]
mov.l #5, [r2]
# CHECK: mov.l #5, [r2] ; encoding: [0xf8,0x26,0x05]
mov.b #5, [r9]
# CHECK: mov.b #5, [r9] ; encoding: [0xf8,0x94,0x05]
mov.b #5, 0[r9]
# CHECK: mov.b #5, [r9] ; encoding: [0xf8,0x94,0x05]
mov.b #-1, [r9]
# CHECK: mov.b #-1, [r9] ; encoding: [0xf8,0x94,0xff]
mov.b #-1, 4[r2]
# CHECK: mov.b #-1, 4[r2] ; encoding: [0xf9,0x24,0x04,0xff]
mov.b #255, 200[r9]
# CHECK: mov.b #255, 200[r9] ; encoding: [0xf9,0x94,0xc8,0xff]
mov.b #5, 1000[r9]
# CHECK: mov.b #5, 1000[r9] ; encoding: [0xfa,0x94,0xe8,0x03,0x05]
mov.b #1, 4[r14]
# CHECK: mov.b #1, 4[r14] ; encoding: [0xf9,0xe4,0x04,0x01]
mov.w #-5, [r9]
# CHECK: mov.w #-5, [r9] ; encoding: [0xf8,0x95,0xfb]
mov.w #-1, 4[r2]
# CHECK: mov.w #-1, 4[r2] ; encoding: [0xf9,0x25,0x02,0xff]
mov.w #0xffff, 4[r2]
# CHECK: mov.w #65535, 4[r2] ; encoding: [0xf9,0x25,0x02,0xff]
mov.w #0xffff, [r9]
# CHECK: mov.w #65535, [r9] ; encoding: [0xf8,0x95,0xff]
mov.w #-129, 4[r2]
# CHECK: mov.w #-129, 4[r2] ; encoding: [0xf9,0x29,0x02,0x7f,0xff]
mov.w #1000, [r9]
# CHECK: mov.w #1000, [r9] ; encoding: [0xf8,0x99,0xe8,0x03]
mov.w #-1000, 20[r9]
# CHECK: mov.w #-1000, 20[r9] ; encoding: [0xf9,0x99,0x0a,0x18,0xfc]
mov.l #0, [r14]
# CHECK: mov.l #0, [r14] ; encoding: [0xf8,0xe6,0x00]
mov.l #-5, [r9]
# CHECK: mov.l #-5, [r9] ; encoding: [0xf8,0x96,0xfb]
mov.l #-1, 124[r2]
# CHECK: mov.l #-1, 124[r2] ; encoding: [0xf9,0x26,0x1f,0xff]
mov.l #0xffffffff, [r9]
# CHECK: mov.l #4294967295, [r9] ; encoding: [0xf8,0x96,0xff]
mov.l #256, 124[r2]
# CHECK: mov.l #256, 124[r2] ; encoding: [0xf9,0x2a,0x1f,0x00,0x01]
mov.l #300, [r2]
# CHECK: mov.l #300, [r2] ; encoding: [0xf8,0x2a,0x2c,0x01]
mov.l #1000, 20[r9]
# CHECK: mov.l #1000, 20[r9] ; encoding: [0xf9,0x9a,0x05,0xe8,0x03]
mov.l #0x7fffff, [r2]
# CHECK: mov.l #8388607, [r2] ; encoding: [0xf8,0x2e,0xff,0xff,0x7f]
mov.l #-0x800000, [r2]
# CHECK: mov.l #-8388608, [r2] ; encoding: [0xf8,0x2e,0x00,0x00,0x80]
mov.l #0x800000, [r2]
# CHECK: mov.l #8388608, [r2] ; encoding: [0xf8,0x22,0x00,0x00,0x80,0x00]
mov.l #100000, [r9]
# CHECK: mov.l #100000, [r9] ; encoding: [0xf8,0x9e,0xa0,0x86,0x01]
mov.l #200, 2000[r2]
# CHECK: mov.l #200, 2000[r2] ; encoding: [0xfa,0x2a,0xf4,0x01,0xc8,0x00]
mov.l #0x12345678, 3000[r9]
# CHECK: mov.l #305419896, 3000[r9] ; encoding: [0xfa,0x92,0xee,0x02,0x78,0x56,0x34,0x12]

# --- memory to memory (a zero displacement is dropped)
mov.b [r1], [r2]
# CHECK: mov.b [r1], [r2] ; encoding: [0xc0,0x12]
mov.w [r1], [r2]
# CHECK: mov.w [r1], [r2] ; encoding: [0xd0,0x12]
mov.l [r1], [r2]
# CHECK: mov.l [r1], [r2] ; encoding: [0xe0,0x12]
mov.l 0[r1], 0[r2]
# CHECK: mov.l [r1], [r2] ; encoding: [0xe0,0x12]
mov.l [r1], 0[r9]
# CHECK: mov.l [r1], [r9] ; encoding: [0xe0,0x19]
mov.l 4[r1], [r2]
# CHECK: mov.l 4[r1], [r2] ; encoding: [0xe1,0x12,0x01]
mov.l [r1], 8[r2]
# CHECK: mov.l [r1], 8[r2] ; encoding: [0xe4,0x12,0x02]
mov.b 3[r1], 1000[r2]
# CHECK: mov.b 3[r1], 1000[r2] ; encoding: [0xc9,0x12,0x03,0xe8,0x03]
mov.w 510[r1], 2[r2]
# CHECK: mov.w 510[r1], 2[r2] ; encoding: [0xd5,0x12,0xff,0x01]
mov.l 400[r1], 80000[r2]
# CHECK: mov.l 400[r1], 80000[r2] ; encoding: [0xe9,0x12,0x64,0x20,0x4e]
