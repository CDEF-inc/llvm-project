# RUN: llvm-mc -triple=rx-none-elf -mcpu=rxv3 -show-encoding %s \
# RUN:   | FileCheck %s
# RUN: llvm-mc -triple=rx-none-elf -mcpu=rxv3e -show-encoding %s \
# RUN:   | FileCheck %s

# RXv3 bit-field move and register bank save / restore.
# The encodings match GNU as 2.43 (rx-elf).
bfmov #0, #0, #1, r1, r2
# CHECK: bfmov #0, #0, #1, r1, r2 ; encoding: [0xfc,0x5e,0x12,0x00,0x04]
bfmov #4, #8, #8, r3, r4
# CHECK: bfmov #4, #8, #8, r3, r4 ; encoding: [0xfc,0x5e,0x34,0x04,0x41]
bfmov #0, #16, #16, r5, r6
# CHECK: bfmov #0, #16, #16, r5, r6 ; encoding: [0xfc,0x5e,0x56,0x10,0x02]
bfmov #31, #0, #1, r7, r8
# CHECK: bfmov #31, #0, #1, r7, r8 ; encoding: [0xfc,0x5e,0x78,0x01,0x04]
bfmov #0, #31, #1, r9, r10
# CHECK: bfmov #0, #31, #1, r9, r10 ; encoding: [0xfc,0x5e,0x9a,0xff,0x03]
bfmov #8, #0, #24, r15, r0
# CHECK: bfmov #8, #0, #24, r15, r0 ; encoding: [0xfc,0x5e,0xf0,0x18,0x60]
bfmovz #0, #0, #1, r1, r2
# CHECK: bfmovz #0, #0, #1, r1, r2 ; encoding: [0xfc,0x5a,0x12,0x00,0x04]
bfmovz #3, #5, #7, r11, r12
# CHECK: bfmovz #3, #5, #7, r11, r12 ; encoding: [0xfc,0x5a,0xbc,0xa2,0x30]
bfmovz #16, #0, #16, r13, r14
# CHECK: bfmovz #16, #0, #16, r13, r14 ; encoding: [0xfc,0x5a,0xde,0x10,0x40]
save r1
# CHECK: save r1 ; encoding: [0xfd,0x76,0xc1,0x00]
save r15
# CHECK: save r15 ; encoding: [0xfd,0x76,0xcf,0x00]
save #0
# CHECK: save #0 ; encoding: [0xfd,0x76,0xe0,0x00]
save #255
# CHECK: save #255 ; encoding: [0xfd,0x76,0xe0,0xff]
rstr r2
# CHECK: rstr r2 ; encoding: [0xfd,0x76,0xd2,0x00]
rstr r0
# CHECK: rstr r0 ; encoding: [0xfd,0x76,0xd0,0x00]
rstr #1
# CHECK: rstr #1 ; encoding: [0xfd,0x76,0xf0,0x01]
rstr #200
# CHECK: rstr #200 ; encoding: [0xfd,0x76,0xf0,0xc8]
