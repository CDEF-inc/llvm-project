# REQUIRES: rx
## Linker relaxation (llvm-mc -mattr=+relax, R_RX_RH_RELAX): bsr.a to a
## near function becomes bsr.w, mov.l #sym takes the smallest immediate the
## address fits, branches over the shortened code and label differences
## follow, alignment padding keeps the next function aligned, and symbols
## move and shrink with the code.

# RUN: llvm-mc -filetype=obj -triple=rx -mattr=+relax %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck %s --check-prefix=RELOC
# RUN: ld.lld %t.o -Ttext=0x1000 -Tdata=0x2000 --defsym=far=0x100000 \
# RUN:   --defsym=small=0x7f -o %t
# RUN: llvm-objdump -d %t | FileCheck %s
# RUN: llvm-objdump -s -j .data %t | FileCheck %s --check-prefix=DATA
# RUN: llvm-readelf -s %t | FileCheck %s --check-prefix=SYM
# RUN: ld.lld %t.o -Ttext=0x1000 -Tdata=0x2000 --defsym=far=0x100000 \
# RUN:   --defsym=small=0x7f --no-relax -o %t.norelax
# RUN: llvm-objdump -d %t.norelax | FileCheck %s --check-prefix=NORELAX

## GNU as -relax's addends: BRA | 1 relocation, IMM12 | 1; LLVM's alignment
## hint (RX_RELAXA_ALIGN | 4) at the start of the 3 bytes of padding.
# RELOC:      R_RX_RH_RELAX - 0x201
# RELOC-NEXT: R_RX_DIR24S_PCREL g
# RELOC:      R_RX_RH_RELAX - 0x21
# RELOC-NEXT: R_RX_DIR32 var
# RELOC:      R_RX_RH_RELAX - 0x21
# RELOC-NEXT: R_RX_DIR32 small
# RELOC:      R_RX_RH_RELAX - 0x10000004

  .text
  .global _start
  .type _start, @function
_start:
## A branch over the shortened code. The assembler sizes it from the
## distance before relaxation (2 + 4 + 6 = 12: bne.b), which can only
## shrink: 2 + 3 (bsr.w) + 4 (mov.l #simm16) = 9.
  bne 1f
  bsr.a g
  mov.l #var, r1
1:
## an address that fits 8 bits: mov.l #simm8 (3 bytes)
  mov.l #small, r2
## too far for bsr.w: stays
  bsr.a far
  rts
2:
  .size _start, . - _start
## 3 NOPs of padding before g; with _start 17 bytes long, all 3 stay
  .p2align 2
  .type g, @function
g:
  rts
  .size g, . - g

# CHECK:      <_start>:
# CHECK-NEXT: 1000: 21 09{{ +}}bne.b 0x1009
# CHECK-NEXT: 1002: 39 12 00{{ +}}bsr.w 0x1014 <g>
# CHECK-NEXT: 1005: fb 1a 04 20{{ +}}mov.l #8196, r1
# CHECK:      1009: fb 26 7f{{ +}}mov.l #127, r2
# CHECK-NEXT: 100c: 05 f4 ef 0f{{ +}}bsr.a 0x100000
# CHECK-NEXT: 1010: 02{{ +}}rts
# CHECK:      1011: 03{{ +}}nop
# CHECK-NEXT: 1012: 03{{ +}}nop
# CHECK-NEXT: 1013: 03{{ +}}nop
# CHECK:      <g>:
# CHECK-NEXT: 1014: 02{{ +}}rts

# NORELAX:      1000: 21 0c{{ +}}bne.b 0x100c
# NORELAX-NEXT: 1002: 05 {{.*}} bsr.a
# NORELAX-NEXT: 1006: fb 12 04 20 00 00{{ +}}mov.l #8196, r1
# NORELAX:      100c: fb 22 7f 00 00 00{{ +}}mov.l #127, r2

  .data
  .long 0
var:
## the length of _start: a difference across shortened code
  .long 2b - _start

# DATA:      Contents of section .data:
# DATA-NEXT: 2000 00000000 11000000

# SYM-DAG: 00001000 17 FUNC GLOBAL DEFAULT {{.*}} _start
# SYM-DAG: 00001014 1 FUNC LOCAL DEFAULT {{.*}} g
