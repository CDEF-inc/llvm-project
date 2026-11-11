# RUN: llvm-mc -triple=rx-none-elf -mcpu=rxv2 -show-encoding %s \
# RUN:   | FileCheck %s
# RUN: llvm-mc -triple=rx-none-elf -mcpu=rxv3e -show-encoding %s \
# RUN:   | FileCheck %s

# DSP (accumulator) instructions: the RXv1 forms with the implicit A0, then
# the forms that name A0 / A1.
# The instructions that need rxv2.
# The encodings match GNU as 2.43 (rx-elf).

# RXv2
mulhi r1, r2, a0
# CHECK: mulhi r1, r2, a0 ; encoding: [0xfd,0x00,0x12]
mulhi r1, r2, a1
# CHECK: mulhi r1, r2, a1 ; encoding: [0xfd,0x08,0x12]
mullo r3, r4, a1
# CHECK: mullo r3, r4, a1 ; encoding: [0xfd,0x09,0x34]
mullh r5, r6, a0
# CHECK: mullh r5, r6, a0 ; encoding: [0xfd,0x02,0x56]
mullh r5, r6, a1
# CHECK: mullh r5, r6, a1 ; encoding: [0xfd,0x0a,0x56]
emula r7, r8, a1
# CHECK: emula r7, r8, a1 ; encoding: [0xfd,0x0b,0x78]
machi r9, r10, a1
# CHECK: machi r9, r10, a1 ; encoding: [0xfd,0x0c,0x9a]
maclo r11, r12, a1
# CHECK: maclo r11, r12, a1 ; encoding: [0xfd,0x0d,0xbc]
maclh r13, r14, a0
# CHECK: maclh r13, r14, a0 ; encoding: [0xfd,0x06,0xde]
maclh r13, r14, a1
# CHECK: maclh r13, r14, a1 ; encoding: [0xfd,0x0e,0xde]
emaca r15, r0, a1
# CHECK: emaca r15, r0, a1 ; encoding: [0xfd,0x0f,0xf0]
msbhi r1, r2, a0
# CHECK: msbhi r1, r2, a0 ; encoding: [0xfd,0x44,0x12]
msbhi r1, r2, a1
# CHECK: msbhi r1, r2, a1 ; encoding: [0xfd,0x4c,0x12]
msblo r3, r4, a1
# CHECK: msblo r3, r4, a1 ; encoding: [0xfd,0x4d,0x34]
msblh r5, r6, a1
# CHECK: msblh r5, r6, a1 ; encoding: [0xfd,0x4e,0x56]
emsba r7, r8, a0
# CHECK: emsba r7, r8, a0 ; encoding: [0xfd,0x47,0x78]
emsba r7, r8, a1
# CHECK: emsba r7, r8, a1 ; encoding: [0xfd,0x4f,0x78]
mvtachi r1, a0
# CHECK: mvtachi r1, a0 ; encoding: [0xfd,0x17,0x01]
mvtachi r1, a1
# CHECK: mvtachi r1, a1 ; encoding: [0xfd,0x17,0x81]
mvtaclo r2, a1
# CHECK: mvtaclo r2, a1 ; encoding: [0xfd,0x17,0x92]
mvtacgu r3, a0
# CHECK: mvtacgu r3, a0 ; encoding: [0xfd,0x17,0x33]
mvtacgu r3, a1
# CHECK: mvtacgu r3, a1 ; encoding: [0xfd,0x17,0xb3]
mvfachi #0, a0, r1
# CHECK: mvfachi #0, a0, r1 ; encoding: [0xfd,0x1f,0x01]
mvfachi #1, a0, r1
# CHECK: mvfachi #1, a0, r1 ; encoding: [0xfd,0x1f,0x41]
mvfachi #2, a0, r1
# CHECK: mvfachi #2, a0, r1 ; encoding: [0xfd,0x1e,0x01]
mvfachi #2, a1, r15
# CHECK: mvfachi #2, a1, r15 ; encoding: [0xfd,0x1e,0x8f]
mvfacmi #0, a1, r2
# CHECK: mvfacmi #0, a1, r2 ; encoding: [0xfd,0x1f,0xa2]
mvfacmi #1, a1, r2
# CHECK: mvfacmi #1, a1, r2 ; encoding: [0xfd,0x1f,0xe2]
mvfaclo #2, a1, r3
# CHECK: mvfaclo #2, a1, r3 ; encoding: [0xfd,0x1e,0x93]
mvfacgu #0, a0, r4
# CHECK: mvfacgu #0, a0, r4 ; encoding: [0xfd,0x1f,0x34]
mvfacgu #1, a1, r4
# CHECK: mvfacgu #1, a1, r4 ; encoding: [0xfd,0x1f,0xf4]
mvfacgu #2, a1, r4
# CHECK: mvfacgu #2, a1, r4 ; encoding: [0xfd,0x1e,0xb4]
racw #1, a1
# CHECK: racw #1, a1 ; encoding: [0xfd,0x18,0x80]
racw #2, a1
# CHECK: racw #2, a1 ; encoding: [0xfd,0x18,0x90]
rdacw #1, a0
# CHECK: rdacw #1, a0 ; encoding: [0xfd,0x18,0x40]
rdacw #2, a1
# CHECK: rdacw #2, a1 ; encoding: [0xfd,0x18,0xd0]
racl #1, a0
# CHECK: racl #1, a0 ; encoding: [0xfd,0x19,0x00]
racl #2, a1
# CHECK: racl #2, a1 ; encoding: [0xfd,0x19,0x90]
rdacl #1, a1
# CHECK: rdacl #1, a1 ; encoding: [0xfd,0x19,0xc0]
rdacl #2, a0
# CHECK: rdacl #2, a0 ; encoding: [0xfd,0x19,0x50]
