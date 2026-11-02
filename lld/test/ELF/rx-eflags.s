# REQUIRES: rx
## e_flags as GNU ld merges them by default (--no-flag-mismatch-warnings):
## the union of the ABI / 64-bit doubles / DSP / PID bits, and the highest
## instruction set of the inputs.

# RUN: llvm-mc -filetype=obj -triple=rx -mcpu=rxv1 %s -o %t1.o
# RUN: llvm-mc -filetype=obj -triple=rx -mcpu=rxv2 %s -o %t2.o
# RUN: llvm-mc -filetype=obj -triple=rx -mcpu=rxv3 %s -o %t3.o
# RUN: llvm-mc -filetype=obj -triple=rx -mcpu=rxv1 -mattr=+64bit-doubles %s -o %td.o
# RUN: ld.lld %t1.o -o %t1 && llvm-readelf -h %t1 | FileCheck %s --check-prefix=V1
# RUN: ld.lld %t1.o %t2.o -o %t12 && llvm-readelf -h %t12 | FileCheck %s --check-prefix=V2
# RUN: ld.lld %t2.o %t3.o %t1.o -o %t123 && llvm-readelf -h %t123 | FileCheck %s --check-prefix=V3
# RUN: ld.lld %t1.o %td.o -o %t1d && llvm-readelf -h %t1d | FileCheck %s --check-prefix=D

# V1: Flags: 0x8
# V2: Flags: 0x108
# V3: Flags: 0x208
# D:  Flags: 0x9

  .text
  .weak _start
_start:
  rts
