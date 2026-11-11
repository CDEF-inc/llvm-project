# RUN: llvm-mc -triple=rx-none-elf -mcpu=rxv1 -show-encoding %s \
# RUN:   | FileCheck %s
# RUN: llvm-mc -triple=rx-none-elf -mcpu=rxv3e -show-encoding %s \
# RUN:   | FileCheck %s

# The encodings match GNU as 2.43 (rx-elf).
sceq.l r1
# CHECK: sceq.l r1 ; encoding: [0xfc,0xdb,0x10]
scne.l r15
# CHECK: scne.l r15 ; encoding: [0xfc,0xdb,0xf1]
scgeu.l r2
# CHECK: scgeu.l r2 ; encoding: [0xfc,0xdb,0x22]
scltu.l r3
# CHECK: scltu.l r3 ; encoding: [0xfc,0xdb,0x33]
scgtu.l r4
# CHECK: scgtu.l r4 ; encoding: [0xfc,0xdb,0x44]
scleu.l r5
# CHECK: scleu.l r5 ; encoding: [0xfc,0xdb,0x55]
scpz.l r6
# CHECK: scpz.l r6 ; encoding: [0xfc,0xdb,0x66]
scn.l r7
# CHECK: scn.l r7 ; encoding: [0xfc,0xdb,0x77]
scge.l r8
# CHECK: scge.l r8 ; encoding: [0xfc,0xdb,0x88]
sclt.l r9
# CHECK: sclt.l r9 ; encoding: [0xfc,0xdb,0x99]
scgt.l r10
# CHECK: scgt.l r10 ; encoding: [0xfc,0xdb,0xaa]
scle.l r11
# CHECK: scle.l r11 ; encoding: [0xfc,0xdb,0xbb]
sco.l r12
# CHECK: sco.l r12 ; encoding: [0xfc,0xdb,0xcc]
scno.l r13
# CHECK: scno.l r13 ; encoding: [0xfc,0xdb,0xdd]
