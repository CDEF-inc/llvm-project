# REQUIRES: rx
## A PC-relative branch to an undefined weak symbol branches to itself
## (displacement 0), so that it stays in range wherever the code is. GCC's
## crtbegin.o calls __register_frame_info that way (after checking that it
## is there); address 0 is 16 MB away from code at 0x01000000. A direct
## reference still gets 0.

# RUN: llvm-mc -filetype=obj -triple=rx %s -o %t.o
# RUN: ld.lld %t.o -Ttext=0x01000000 -o %t
# RUN: llvm-objdump -d %t | FileCheck %s
# RUN: llvm-objdump -s -j .data %t | FileCheck %s --check-prefix=DATA

# CHECK:      1000000: 05 00 00 00   bsr.a 0x1000000
# CHECK-NEXT: 1000004: 39 00 00      bsr.w 0x1000004
# CHECK-NEXT: 1000007: 2e 00         bra.b 0x1000007
# DATA: 00000000

  .text
  .global _start
_start:
  bsr.a undef
  bsr.w undef
  bra.b undef

  .data
  .long undef

  .weak undef
