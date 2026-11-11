# RUN: llvm-mc -triple=rx-none-elf -mcpu=rxv3e -show-encoding %s \
# RUN:   | FileCheck %s

# RXv3e Protect Zone instructions, INT Rs, the 5-bit MVTIPL and the new
# control registers. GNU as 2.43 has
# no RXv3e, so the RXv3e forms are compared with the encoding in the RXv3e
# manual (R01US0745EJ0101, section 4).
trcall #0
# CHECK: trcall #0 ; encoding: [0xfd,0x75,0x00,0x00]
trcall #128
# CHECK: trcall #128 ; encoding: [0xfd,0x75,0x00,0x80]
trcall #255
# CHECK: trcall #255 ; encoding: [0xfd,0x75,0x00,0xff]
trrte
# CHECK: trrte ; encoding: [0x7f,0x97]
trmvfc ivc, r1
# CHECK: trmvfc ivc, r1 ; encoding: [0xfd,0x5a,0x51]
trmvfc bpsw, r2
# CHECK: trmvfc bpsw, r2 ; encoding: [0xfd,0x5a,0x82]
trmvfc bpc, r3
# CHECK: trmvfc bpc, r3 ; encoding: [0xfd,0x5a,0x93]
trmvfc isp, r4
# CHECK: trmvfc isp, r4 ; encoding: [0xfd,0x5a,0xa4]
trmvfc intb, r5
# CHECK: trmvfc intb, r5 ; encoding: [0xfd,0x5a,0xc5]
trmvfc extb, r6
# CHECK: trmvfc extb, r6 ; encoding: [0xfd,0x5a,0xd6]
trmvfc trctb, r0
# CHECK: trmvfc trctb, r0 ; encoding: [0xfd,0x5a,0xe0]
trmvfc wintb, r15
# CHECK: trmvfc wintb, r15 ; encoding: [0xfd,0x5a,0xff]
trmvtc r1, ivc
# CHECK: trmvtc r1, ivc ; encoding: [0xfd,0x58,0x15]
trmvtc r2, bpsw
# CHECK: trmvtc r2, bpsw ; encoding: [0xfd,0x58,0x28]
trmvtc r3, bpc
# CHECK: trmvtc r3, bpc ; encoding: [0xfd,0x58,0x39]
trmvtc r4, isp
# CHECK: trmvtc r4, isp ; encoding: [0xfd,0x58,0x4a]
trmvtc r5, intb
# CHECK: trmvtc r5, intb ; encoding: [0xfd,0x58,0x5c]
trmvtc r6, extb
# CHECK: trmvtc r6, extb ; encoding: [0xfd,0x58,0x6d]
trmvtc r0, trctb
# CHECK: trmvtc r0, trctb ; encoding: [0xfd,0x58,0x0e]
trmvtc r15, wintb
# CHECK: trmvtc r15, wintb ; encoding: [0xfd,0x58,0xff]
trmvtc #0, ivc
# CHECK: trmvtc #0, ivc ; encoding: [0xfd,0x77,0x25,0x00]
trmvtc #127, intb
# CHECK: trmvtc #127, intb ; encoding: [0xfd,0x77,0x2c,0x7f]
trmvtc #-128, intb
# CHECK: trmvtc #-128, intb ; encoding: [0xfd,0x77,0x2c,0x80]
trmvtc #128, intb
# CHECK: trmvtc #128, intb ; encoding: [0xfd,0x7b,0x2c,0x80,0x00]
trmvtc #-32768, extb
# CHECK: trmvtc #-32768, extb ; encoding: [0xfd,0x7b,0x2d,0x00,0x80]
trmvtc #32768, bpsw
# CHECK: trmvtc #32768, bpsw ; encoding: [0xfd,0x7f,0x28,0x00,0x80,0x00]
trmvtc #-8388608, bpc
# CHECK: trmvtc #-8388608, bpc ; encoding: [0xfd,0x7f,0x29,0x00,0x00,0x80]
trmvtc #8388608, isp
# CHECK: trmvtc #8388608, isp ; encoding: [0xfd,0x73,0x2a,0x00,0x00,0x80,0x00]
trmvtc #0x80000000, trctb
# CHECK: trmvtc #2147483648, trctb ; encoding: [0xfd,0x73,0x2e,0x00,0x00,0x00,0x80]
trmvtc #0x12345678, wintb
# CHECK: trmvtc #305419896, wintb ; encoding: [0xfd,0x73,0x2f,0x78,0x56,0x34,0x12]
int r0
# CHECK: int r0 ; encoding: [0xfd,0x75,0x10,0x00]
int r7
# CHECK: int r7 ; encoding: [0xfd,0x75,0x17,0x00]
int r15
# CHECK: int r15 ; encoding: [0xfd,0x75,0x1f,0x00]
int #3
# CHECK: int #3 ; encoding: [0x75,0x60,0x03]
mvtipl #0
# CHECK: mvtipl #0 ; encoding: [0x75,0x70,0x00]
mvtipl #15
# CHECK: mvtipl #15 ; encoding: [0x75,0x70,0x0f]
mvtipl #16
# CHECK: mvtipl #16 ; encoding: [0x75,0x70,0x10]
mvtipl #31
# CHECK: mvtipl #31 ; encoding: [0x75,0x70,0x1f]
mvtc r1, ivc
# CHECK: mvtc r1, ivc ; encoding: [0xfd,0x68,0x15]
mvtc r2, wpri
# CHECK: mvtc r2, wpri ; encoding: [0xfd,0x68,0x2e]
mvtc r3, wid
# CHECK: mvtc r3, wid ; encoding: [0xfd,0x68,0x3f]
mvtc #1, ivc
# CHECK: mvtc #1, ivc ; encoding: [0xfd,0x77,0x05,0x01]
mvtc #0x1000, wpri
# CHECK: mvtc #4096, wpri ; encoding: [0xfd,0x7b,0x0e,0x00,0x10]
mvtc #0x80000000, wid
# CHECK: mvtc #2147483648, wid ; encoding: [0xfd,0x73,0x0f,0x00,0x00,0x00,0x80]
mvfc ivc, r1
# CHECK: mvfc ivc, r1 ; encoding: [0xfd,0x6a,0x51]
mvfc wpri, r2
# CHECK: mvfc wpri, r2 ; encoding: [0xfd,0x6a,0xe2]
mvfc wid, r15
# CHECK: mvfc wid, r15 ; encoding: [0xfd,0x6a,0xff]
pushc ivc
# CHECK: pushc ivc ; encoding: [0x7e,0xc5]
pushc wpri
# CHECK: pushc wpri ; encoding: [0x7e,0xce]
pushc wid
# CHECK: pushc wid ; encoding: [0x7e,0xcf]
popc ivc
# CHECK: popc ivc ; encoding: [0x7e,0xe5]
popc wpri
# CHECK: popc wpri ; encoding: [0x7e,0xee]
popc wid
# CHECK: popc wid ; encoding: [0x7e,0xef]
