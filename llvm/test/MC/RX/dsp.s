# RUN: llvm-mc -triple=rx-none-elf -mcpu=rxv1 -show-encoding %s \
# RUN:   | FileCheck %s
# RUN: llvm-mc -triple=rx-none-elf -mcpu=rxv3e -show-encoding %s \
# RUN:   | FileCheck %s

# DSP (accumulator) instructions: the RXv1 forms with the implicit A0, then
# the forms that name A0 / A1.
# The encodings match GNU as 2.43 (rx-elf).
mulhi r1, r2
# CHECK: mulhi r1, r2 ; encoding: [0xfd,0x00,0x12]
mullo r3, r4
# CHECK: mullo r3, r4 ; encoding: [0xfd,0x01,0x34]
machi r5, r6
# CHECK: machi r5, r6 ; encoding: [0xfd,0x04,0x56]
maclo r7, r8
# CHECK: maclo r7, r8 ; encoding: [0xfd,0x05,0x78]
mvtachi r9
# CHECK: mvtachi r9 ; encoding: [0xfd,0x17,0x09]
mvtaclo r10
# CHECK: mvtaclo r10 ; encoding: [0xfd,0x17,0x1a]
mvfachi r11
# CHECK: mvfachi r11 ; encoding: [0xfd,0x1f,0x0b]
mvfacmi r12
# CHECK: mvfacmi r12 ; encoding: [0xfd,0x1f,0x2c]
mvfaclo r13
# CHECK: mvfaclo r13 ; encoding: [0xfd,0x1f,0x1d]
racw #1
# CHECK: racw #1 ; encoding: [0xfd,0x18,0x00]
racw #2
# CHECK: racw #2 ; encoding: [0xfd,0x18,0x10]
