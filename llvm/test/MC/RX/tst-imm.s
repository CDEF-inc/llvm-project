# RUN: llvm-mc -triple=rx-none-elf -mcpu=rxv1 -show-encoding %s \
# RUN:   | FileCheck %s
# RUN: llvm-mc -triple=rx-none-elf -mcpu=rxv3e -show-encoding %s \
# RUN:   | FileCheck %s

# TST #imm, Rd; the shortest immediate is chosen. The encodings match GNU as
# 2.43 (rx-elf).
tst #1, r5
# CHECK: tst #1, r5 ; encoding: [0xfd,0x74,0xc5,0x01]
tst #-1, r5
# CHECK: tst #-1, r5 ; encoding: [0xfd,0x74,0xc5,0xff]
tst #128, r5
# CHECK: tst #128, r5 ; encoding: [0xfd,0x78,0xc5,0x80,0x00]
tst #0x8000, r5
# CHECK: tst #32768, r5 ; encoding: [0xfd,0x7c,0xc5,0x00,0x80,0x00]
tst #0x800000, r5
# CHECK: tst #8388608, r5 ; encoding: [0xfd,0x70,0xc5,0x00,0x00,0x80,0x00]
tst #0x80000000, r15
# CHECK: tst #2147483648, r15 ; encoding: [0xfd,0x70,0xcf,0x00,0x00,0x00,0x80]
