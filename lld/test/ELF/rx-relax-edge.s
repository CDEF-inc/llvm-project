# REQUIRES: rx
## Linker relaxation edge cases: an address in the top 32 KiB is a negative
## 16-bit immediate, and a branch across alignment padding is not a .s
## branch, as the deleted padding could bring it below its distance of 3.

# RUN: llvm-mc -filetype=obj -triple=rx -mattr=+relax %s -o %t.o
# RUN: llvm-objdump -d %t.o | FileCheck %s --check-prefix=OBJ
# RUN: ld.lld %t.o -Ttext=0x1000 --defsym=high=0xffff8000 -o %t
# RUN: llvm-objdump -d %t | FileCheck %s
# RUN: ld.lld %t.o -Ttext=0x1000 --defsym=high=0xffff8000 --no-relax -o %t.norelax
# RUN: llvm-objdump -d %t.norelax | FileCheck %s --check-prefix=NORELAX

  .text
  .global _start
  .type _start, @function
_start:
  mov.l #high, r3
  nop
  nop
## 3 NOPs of padding follow; a bra.s over them (4) would become 1.
  bra 1f
  .p2align 2
1:
  rts
  .size _start, . - _start

# OBJ:      2e 00{{ +}}bra.b

# CHECK:      1000: fb 3a 00 80{{ +}}mov.l #-32768, r3
# CHECK-NEXT: 1004: 03{{ +}}nop
# CHECK-NEXT: 1005: 03{{ +}}nop
# CHECK-NEXT: 1006: 2e 02{{ +}}bra.b 0x1008
# CHECK:      1008: 02{{ +}}rts

# NORELAX:      1000: fb 32 00 80 ff ff{{ +}}mov.l #4294934528, r3
# NORELAX:      1008: 2e 04{{ +}}bra.b 0x100c
# NORELAX:      100c: 02{{ +}}rts
