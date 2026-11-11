# RUN: llvm-mc -triple=rx-none-elf -mcpu=rxv3 -show-encoding %s \
# RUN:   | FileCheck %s
# RUN: llvm-mc -triple=rx-none-elf -mcpu=rxv3e -show-encoding %s \
# RUN:   | FileCheck %s

# RXv3 double-precision FPU instructions.
# The encodings match GNU as 2.43 (rx-elf),
# except on the lines marked "; manual": GNU as encodes those wrongly, and the
# bytes are the ones in the RX manual and CC-RX.
dadd dr1, dr2, dr3
# CHECK: dadd dr1, dr2, dr3 ; encoding: [0x76,0x90,0x20,0x31]
dsub dr15, dr0, dr7
# CHECK: dsub dr15, dr0, dr7 ; encoding: [0x76,0x90,0x01,0x7f]
dmul dr4, dr5, dr6
# CHECK: dmul dr4, dr5, dr6 ; encoding: [0x76,0x90,0x52,0x64]
ddiv dr8, dr9, dr10
# CHECK: ddiv dr8, dr9, dr10 ; encoding: [0x76,0x90,0x95,0xa8]
dabs dr1, dr2
# CHECK: dabs dr1, dr2 ; encoding: [0x76,0x90,0x1c,0x21]
dneg dr3, dr4
# CHECK: dneg dr3, dr4 ; encoding: [0x76,0x90,0x3c,0x42]
dsqrt dr5, dr6
# CHECK: dsqrt dr5, dr6 ; encoding: [0x76,0x90,0x5d,0x60]
dtoi dr7, dr8
# CHECK: dtoi dr7, dr8 ; encoding: [0x76,0x90,0x7d,0x88]
dtou dr9, dr10
# CHECK: dtou dr9, dr10 ; encoding: [0x76,0x90,0x9d,0xa9]
dtof dr11, dr12
# CHECK: dtof dr11, dr12 ; encoding: [0x76,0x90,0xbd,0xcc]
dround dr13, dr14
# CHECK: dround dr13, dr14 ; encoding: [0x76,0x90,0xdd,0xed]
dmov.d dr15, dr0
# CHECK: dmov.d dr15, dr0 ; encoding: [0x76,0x90,0xfc,0x00]
dcmpun dr1, dr2
# CHECK: dcmpun dr1, dr2 ; encoding: [0x76,0x90,0x28,0x11]
dcmpeq dr3, dr4
# CHECK: dcmpeq dr3, dr4 ; encoding: [0x76,0x90,0x48,0x23]
dcmplt dr5, dr6
# CHECK: dcmplt dr5, dr6 ; encoding: [0x76,0x90,0x68,0x45]
dcmple dr7, dr15
# CHECK: dcmple dr7, dr15 ; encoding: [0x76,0x90,0xf8,0x67]

# GNU as 2.43 swaps the Rs and DRd / DCRd fields of ftod / itod / utod / mvtdc
# (its own objdump decodes "itod r3, dr4" as "itod r4, dr3"); these lines
# are checked against the RXv3 manual (section 4: 1000 rs[3:0] rd[3:0] op).
ftod r1, dr2  ; manual
# CHECK: ftod r1, dr2 ; encoding: [0xfd,0x77,0x81,0x2a]
itod r3, dr4  ; manual
# CHECK: itod r3, dr4 ; encoding: [0xfd,0x77,0x83,0x49]
itod r1, dr2  ; manual
# CHECK: itod r1, dr2 ; encoding: [0xfd,0x77,0x81,0x29]
utod r15, dr0  ; manual
# CHECK: utod r15, dr0 ; encoding: [0xfd,0x77,0x8f,0x0d]
dmov.d r1, drh2
# CHECK: dmov.d r1, drh2 ; encoding: [0xfd,0x77,0x81,0x23]
dmov.l r3, drh4
# CHECK: dmov.l r3, drh4 ; encoding: [0xfd,0x77,0x83,0x42]
dmov.l r5, drl6
# CHECK: dmov.l r5, drl6 ; encoding: [0xfd,0x77,0x85,0x60]
dmov.l drh7, r8
# CHECK: dmov.l drh7, r8 ; encoding: [0xfd,0x75,0x88,0x72]
dmov.l drl9, r10
# CHECK: dmov.l drl9, r10 ; encoding: [0xfd,0x75,0x8a,0x90]
dmov.l drh15, r0
# CHECK: dmov.l drh15, r0 ; encoding: [0xfd,0x75,0x80,0xf2]
dmov.d dr1, [r2]
# CHECK: dmov.d dr1, [r2] ; encoding: [0xfc,0x78,0x28,0x10]
dmov.d dr1, 0[r2]
# CHECK: dmov.d dr1, [r2] ; encoding: [0xfc,0x78,0x28,0x10]

# dmov.d scales dsp by 4, not 8 as the manual's note says (RX72M and
# Renesas CC-RX; GNU as follows the manual): bytes from CC-RX V3.08
dmov.d dr3, 4[r4]  ; manual
# CHECK: dmov.d dr3, 4[r4] ; encoding: [0xfc,0x79,0x48,0x01,0x30]
dmov.d dr3, 8[r4]  ; manual
# CHECK: dmov.d dr3, 8[r4] ; encoding: [0xfc,0x79,0x48,0x02,0x30]
dmov.d dr3, 1020[r4]  ; manual
# CHECK: dmov.d dr3, 1020[r4] ; encoding: [0xfc,0x79,0x48,0xff,0x30]
dmov.d dr5, 1024[r6]  ; manual
# CHECK: dmov.d dr5, 1024[r6] ; encoding: [0xfc,0x7a,0x68,0x00,0x01,0x50]
dmov.d dr5, 262140[r6]  ; manual
# CHECK: dmov.d dr5, 262140[r6] ; encoding: [0xfc,0x7a,0x68,0xff,0xff,0x50]
dmov.d [r7], dr8
# CHECK: dmov.d [r7], dr8 ; encoding: [0xfc,0xc8,0x78,0x80]
dmov.d 16[r7], dr8  ; manual
# CHECK: dmov.d 16[r7], dr8 ; encoding: [0xfc,0xc9,0x78,0x04,0x80]
dmov.d 1020[r7], dr8  ; manual
# CHECK: dmov.d 1020[r7], dr8 ; encoding: [0xfc,0xc9,0x78,0xff,0x80]
dmov.d 4096[r9], dr10  ; manual
# CHECK: dmov.d 4096[r9], dr10 ; encoding: [0xfc,0xca,0x98,0x00,0x04,0xa0]
dmov.d 262140[r9], dr10  ; manual
# CHECK: dmov.d 262140[r9], dr10 ; encoding: [0xfc,0xca,0x98,0xff,0xff,0xa0]
dmov.d #0, drh1
# CHECK: dmov.d #0, drh1 ; encoding: [0xf9,0x03,0x13,0x00,0x00,0x00,0x00]
dmov.d #0x3ff00000, drh2
# CHECK: dmov.d #1072693248, drh2 ; encoding: [0xf9,0x03,0x23,0x00,0x00,0xf0,0x3f]
dmov.l #0x12345678, drh3
# CHECK: dmov.l #305419896, drh3 ; encoding: [0xf9,0x03,0x32,0x78,0x56,0x34,0x12]
dmov.l #0xffffffff, drl4
# CHECK: dmov.l #4294967295, drl4 ; encoding: [0xf9,0x03,0x40,0xff,0xff,0xff,0xff]
dmov.l #1, drl15
# CHECK: dmov.l #1, drl15 ; encoding: [0xf9,0x03,0xf0,0x01,0x00,0x00,0x00]
dpushm.d dr0-dr15
# CHECK: dpushm.d dr0-dr15 ; encoding: [0x75,0xb0,0x0f]
dpushm.d dr4-dr4
# CHECK: dpushm.d dr4-dr4 ; encoding: [0x75,0xb0,0x40]
dpushm.d dr8-dr11
# CHECK: dpushm.d dr8-dr11 ; encoding: [0x75,0xb0,0x83]
dpopm.d dr0-dr15
# CHECK: dpopm.d dr0-dr15 ; encoding: [0x75,0xb8,0x0f]
dpopm.d dr8-dr11
# CHECK: dpopm.d dr8-dr11 ; encoding: [0x75,0xb8,0x83]
dpushm.l dpsw-depc
# CHECK: dpushm.l dpsw-depc ; encoding: [0x75,0xa0,0x03]
dpushm.l dcr1-dcr2
# CHECK: dpushm.l dcmr-decnt ; encoding: [0x75,0xa0,0x11]
dpopm.l dpsw-depc
# CHECK: dpopm.l dpsw-depc ; encoding: [0x75,0xa8,0x03]
dpopm.l dcmr-dcmr
# CHECK: dpopm.l dcmr-dcmr ; encoding: [0x75,0xa8,0x10]
mvtdc r1, dpsw  ; manual
# CHECK: mvtdc r1, dpsw ; encoding: [0xfd,0x77,0x81,0x04]
mvtdc r2, dcmr  ; manual
# CHECK: mvtdc r2, dcmr ; encoding: [0xfd,0x77,0x82,0x14]
mvtdc r3, decnt  ; manual
# CHECK: mvtdc r3, decnt ; encoding: [0xfd,0x77,0x83,0x24]
mvtdc r4, depc  ; manual
# CHECK: mvtdc r4, depc ; encoding: [0xfd,0x77,0x84,0x34]
mvtdc r5, dcr0  ; manual
# CHECK: mvtdc r5, dpsw ; encoding: [0xfd,0x77,0x85,0x04]
mvtdc r7, dcmr  ; manual
# CHECK: mvtdc r7, dcmr ; encoding: [0xfd,0x77,0x87,0x14]
mvfdc dpsw, r6
# CHECK: mvfdc dpsw, r6 ; encoding: [0xfd,0x75,0x86,0x04]
mvfdc depc, r7
# CHECK: mvfdc depc, r7 ; encoding: [0xfd,0x75,0x87,0x34]
mvfdc dcr2, r8
# CHECK: mvfdc decnt, r8 ; encoding: [0xfd,0x75,0x88,0x24]
mvfdr
# CHECK: mvfdr ; encoding: [0x75,0x90,0x1b]
