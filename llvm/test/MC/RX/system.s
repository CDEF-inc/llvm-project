# RUN: llvm-mc -triple=rx-none-elf -mcpu=rxv1 -show-encoding %s \
# RUN:   | FileCheck %s
# RUN: llvm-mc -triple=rx-none-elf -mcpu=rxv3e -show-encoding %s \
# RUN:   | FileCheck %s

# System control instructions.
# The encodings match GNU as 2.43 (rx-elf).
mvtc r1, psw
# CHECK: mvtc r1, psw ; encoding: [0xfd,0x68,0x10]
mvtc r2, usp
# CHECK: mvtc r2, usp ; encoding: [0xfd,0x68,0x22]
mvtc r3, fpsw
# CHECK: mvtc r3, fpsw ; encoding: [0xfd,0x68,0x33]
mvtc r4, bpsw
# CHECK: mvtc r4, bpsw ; encoding: [0xfd,0x68,0x48]
mvtc r5, bpc
# CHECK: mvtc r5, bpc ; encoding: [0xfd,0x68,0x59]
mvtc r15, isp
# CHECK: mvtc r15, isp ; encoding: [0xfd,0x68,0xfa]
mvtc r6, fintv
# CHECK: mvtc r6, fintv ; encoding: [0xfd,0x68,0x6b]
mvtc r7, intb
# CHECK: mvtc r7, intb ; encoding: [0xfd,0x68,0x7c]
mvfc psw, r1
# CHECK: mvfc psw, r1 ; encoding: [0xfd,0x6a,0x01]
mvfc pc, r2
# CHECK: mvfc pc, r2 ; encoding: [0xfd,0x6a,0x12]
mvfc usp, r3
# CHECK: mvfc usp, r3 ; encoding: [0xfd,0x6a,0x23]
mvfc fpsw, r4
# CHECK: mvfc fpsw, r4 ; encoding: [0xfd,0x6a,0x34]
mvfc isp, r15
# CHECK: mvfc isp, r15 ; encoding: [0xfd,0x6a,0xaf]
mvfc intb, r5
# CHECK: mvfc intb, r5 ; encoding: [0xfd,0x6a,0xc5]
mvtc #0, psw
# CHECK: mvtc #0, psw ; encoding: [0xfd,0x77,0x00,0x00]
mvtc #127, fpsw
# CHECK: mvtc #127, fpsw ; encoding: [0xfd,0x77,0x03,0x7f]
mvtc #-1, isp
# CHECK: mvtc #-1, isp ; encoding: [0xfd,0x77,0x0a,0xff]
mvtc #128, isp
# CHECK: mvtc #128, isp ; encoding: [0xfd,0x7b,0x0a,0x80,0x00]
mvtc #0x10000, intb
# CHECK: mvtc #65536, intb ; encoding: [0xfd,0x7f,0x0c,0x00,0x00,0x01]
mvtc #0x7fffff, usp
# CHECK: mvtc #8388607, usp ; encoding: [0xfd,0x7f,0x02,0xff,0xff,0x7f]
mvtc #0x800000, intb
# CHECK: mvtc #8388608, intb ; encoding: [0xfd,0x73,0x0c,0x00,0x00,0x80,0x00]
mvtc #0x17ff0, isp
# CHECK: mvtc #98288, isp ; encoding: [0xfd,0x7f,0x0a,0xf0,0x7f,0x01]
mvtc #0x01fffff0, isp
# CHECK: mvtc #33554416, isp ; encoding: [0xfd,0x73,0x0a,0xf0,0xff,0xff,0x01]
mvtc #0xffffff80, intb
# CHECK: mvtc #4294967168, intb ; encoding: [0xfd,0x77,0x0c,0x80]
pushc psw
# CHECK: pushc psw ; encoding: [0x7e,0xc0]
pushc fpsw
# CHECK: pushc fpsw ; encoding: [0x7e,0xc3]
pushc intb
# CHECK: pushc intb ; encoding: [0x7e,0xcc]
popc psw
# CHECK: popc psw ; encoding: [0x7e,0xe0]
popc isp
# CHECK: popc isp ; encoding: [0x7e,0xea]
mvtipl #0
# CHECK: mvtipl #0 ; encoding: [0x75,0x70,0x00]
mvtipl #7
# CHECK: mvtipl #7 ; encoding: [0x75,0x70,0x07]
mvtipl #15
# CHECK: mvtipl #15 ; encoding: [0x75,0x70,0x0f]
int #0
# CHECK: int #0 ; encoding: [0x75,0x60,0x00]
int #255
# CHECK: int #255 ; encoding: [0x75,0x60,0xff]
setpsw c
# CHECK: setpsw c ; encoding: [0x7f,0xa0]
setpsw z
# CHECK: setpsw z ; encoding: [0x7f,0xa1]
setpsw s
# CHECK: setpsw s ; encoding: [0x7f,0xa2]
setpsw o
# CHECK: setpsw o ; encoding: [0x7f,0xa3]
setpsw i
# CHECK: setpsw i ; encoding: [0x7f,0xa8]
setpsw u
# CHECK: setpsw u ; encoding: [0x7f,0xa9]
clrpsw c
# CHECK: clrpsw c ; encoding: [0x7f,0xb0]
clrpsw z
# CHECK: clrpsw z ; encoding: [0x7f,0xb1]
clrpsw s
# CHECK: clrpsw s ; encoding: [0x7f,0xb2]
clrpsw o
# CHECK: clrpsw o ; encoding: [0x7f,0xb3]
clrpsw i
# CHECK: clrpsw i ; encoding: [0x7f,0xb8]
clrpsw u
# CHECK: clrpsw u ; encoding: [0x7f,0xb9]
