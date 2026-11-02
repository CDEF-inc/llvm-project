# REQUIRES: rx
## The RX relocations, as GNU ld applies them: code and data little-endian,
## PC-relative displacements measured from the start of the instruction
## (the field is one byte into it; the 3-bit one is in the opcode byte), and
## the stack relocations (R_RX_SYM / R_RX_OPsub / R_RX_ABS32) that a
## difference of undefined symbols becomes.

# RUN: llvm-mc -filetype=obj -triple=rx %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck %s --check-prefix=RELOC
# RUN: ld.lld %t.o -Ttext=0x1000 -Tdata=0x2000 --defsym=near=0x1006 \
# RUN:   --defsym=ext=0x1080 --defsym=far=0x7f1234 --defsym=a=0x3000 \
# RUN:   --defsym=b=0x3010 --defsym=small=0x7f -o %t
# RUN: llvm-objdump -d %t | FileCheck %s
# RUN: llvm-objdump -s -j .data %t | FileCheck %s --check-prefix=DATA

# RELOC:      R_RX_DIR3U_PCREL near
# RELOC-NEXT: R_RX_DIR8S_PCREL ext
# RELOC-NEXT: R_RX_DIR16S_PCREL ext
# RELOC-NEXT: R_RX_DIR24S_PCREL far
# RELOC-NEXT: R_RX_DIR32 ext
# RELOC:      R_RX_DIR32 ext
# RELOC-NEXT: R_RX_SYM b
# RELOC-NEXT: R_RX_SYM a
# RELOC-NEXT: R_RX_OPsub a
# RELOC-NEXT: R_RX_ABS32 a
# RELOC-NEXT: R_RX_DIR16 a
# RELOC-NEXT: R_RX_DIR8 small

  .text
  .global _start
_start:
## near - 0x1000 = 6: bra.s #6 is 0000 1 110 = 0x0e
  bra.s near
## ext - 0x1001 = 0x7f
  beq.b ext
## ext - 0x1003 = 0x7d
  bra.w ext
## far - 0x1006 = 0x7f022e
  bsr.a far
## the address of ext
  mov.l #ext, r1

# CHECK:      <_start>:
# CHECK-NEXT: 1000: 0e{{ +}}bra.s
# CHECK-NEXT: 1001: 20 7f{{ +}}beq.b
# CHECK-NEXT: 1003: 38 7d 00{{ +}}bra.w
# CHECK-NEXT: 1006: 05 2e 02 7f{{ +}}bsr.a
# CHECK-NEXT: 100a: fb 12 80 10 00 00{{ +}}mov.l #4224, r1

  .data
  .long ext
## b - a, through the relocation stack
  .long b - a
  .short a + 5
## GNU ld does not support R_RX_DIR8; lld does
  .byte small

# DATA:      Contents of section .data:
# DATA-NEXT: 2000 80100000 10000000 05307f
