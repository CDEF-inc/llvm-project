# RUN: llvm-mc -triple=rx-none-elf -mcpu=rxv2 -show-encoding %s \
# RUN:   | FileCheck %s
# RUN: llvm-mc -triple=rx-none-elf -mcpu=rxv3e -show-encoding %s \
# RUN:   | FileCheck %s

# stz / stnz and the RXv2 exclusive access instructions.
# The encodings match GNU as 2.43 (rx-elf).
stz #0, r1
# CHECK: stz #0, r1 ; encoding: [0xfd,0x74,0xe1,0x00]
stz #127, r2
# CHECK: stz #127, r2 ; encoding: [0xfd,0x74,0xe2,0x7f]
stz #-128, r3
# CHECK: stz #-128, r3 ; encoding: [0xfd,0x74,0xe3,0x80]
stz #128, r4
# CHECK: stz #128, r4 ; encoding: [0xfd,0x78,0xe4,0x80,0x00]
stz #-32768, r5
# CHECK: stz #-32768, r5 ; encoding: [0xfd,0x78,0xe5,0x00,0x80]
stz #0x7fffff, r6
# CHECK: stz #8388607, r6 ; encoding: [0xfd,0x7c,0xe6,0xff,0xff,0x7f]
stz #0x800000, r7
# CHECK: stz #8388608, r7 ; encoding: [0xfd,0x70,0xe7,0x00,0x00,0x80,0x00]
stz #0xffffffff, r8
# CHECK: stz #4294967295, r8 ; encoding: [0xfd,0x74,0xe8,0xff]
stnz #1, r9
# CHECK: stnz #1, r9 ; encoding: [0xfd,0x74,0xf9,0x01]
stnz #-1, r10
# CHECK: stnz #-1, r10 ; encoding: [0xfd,0x74,0xfa,0xff]
stnz #1000, r11
# CHECK: stnz #1000, r11 ; encoding: [0xfd,0x78,0xfb,0xe8,0x03]
stnz #0x12345678, r12
# CHECK: stnz #305419896, r12 ; encoding: [0xfd,0x70,0xfc,0x78,0x56,0x34,0x12]

# RXv2
stz r1, r2
# CHECK: stz r1, r2 ; encoding: [0xfc,0x4b,0x12]
stnz r3, r4
# CHECK: stnz r3, r4 ; encoding: [0xfc,0x4f,0x34]
stz r15, r0
# CHECK: stz r15, r0 ; encoding: [0xfc,0x4b,0xf0]
movli [r1], r2
# CHECK: movli [r1], r2 ; encoding: [0xfd,0x2f,0x12]
movli [r15], r0
# CHECK: movli [r15], r0 ; encoding: [0xfd,0x2f,0xf0]
movco r3, [r4]
# CHECK: movco r3, [r4] ; encoding: [0xfd,0x27,0x43]
movco r0, [r15]
# CHECK: movco r0, [r15] ; encoding: [0xfd,0x27,0xf0]
