# RUN: llvm-mc -triple=rx-none-elf -mcpu=rxv2 -show-encoding %s \
# RUN:   | FileCheck %s
# RUN: llvm-mc -triple=rx-none-elf -mcpu=rxv3e -show-encoding %s \
# RUN:   | FileCheck %s

# Single-precision FPU instructions (RXv1, plus the RXv2 additions).
# The instructions that need rxv2.
# The encodings match GNU as 2.43 (rx-elf).

# RXv2
fadd r1, r2, r3
# CHECK: fadd r1, r2, r3 ; encoding: [0xff,0xa3,0x12]
fsub r4, r5, r6
# CHECK: fsub r4, r5, r6 ; encoding: [0xff,0x86,0x45]
fmul r7, r8, r9
# CHECK: fmul r7, r8, r9 ; encoding: [0xff,0xb9,0x78]
fadd r15, r0, r14
# CHECK: fadd r15, r0, r14 ; encoding: [0xff,0xae,0xf0]
fsqrt r1, r2
# CHECK: fsqrt r1, r2 ; encoding: [0xfc,0xa3,0x12]
fsqrt [r3], r4
# CHECK: fsqrt [r3], r4 ; encoding: [0xfc,0xa0,0x34]
fsqrt 4[r3], r4
# CHECK: fsqrt 4[r3], r4 ; encoding: [0xfc,0xa1,0x34,0x01]
fsqrt 2048[r3], r4
# CHECK: fsqrt 2048[r3], r4 ; encoding: [0xfc,0xa2,0x34,0x00,0x02]
ftou r5, r6
# CHECK: ftou r5, r6 ; encoding: [0xfc,0xa7,0x56]
ftou [r7], r8
# CHECK: ftou [r7], r8 ; encoding: [0xfc,0xa4,0x78]
ftou 252[r7], r8
# CHECK: ftou 252[r7], r8 ; encoding: [0xfc,0xa5,0x78,0x3f]
ftou 256[r7], r8
# CHECK: ftou 256[r7], r8 ; encoding: [0xfc,0xa5,0x78,0x40]
utof r9, r10
# CHECK: utof r9, r10 ; encoding: [0xfc,0x57,0x9a]
utof r0, r15
# CHECK: utof r0, r15 ; encoding: [0xfc,0x57,0x0f]
