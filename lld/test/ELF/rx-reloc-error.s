# REQUIRES: rx
## Out-of-range RX relocations.

# RUN: llvm-mc -filetype=obj -triple=rx %s -o %t.o
# RUN: not ld.lld %t.o -Ttext=0x1000 --defsym=near=0x1020 \
# RUN:   --defsym=ext=0x1200 --defsym=far=0x900000 -o /dev/null 2>&1 | \
# RUN:   FileCheck %s --implicit-check-not=error:

  .text
  .global _start
_start:
## the 3-bit field reaches 3..10 bytes
# CHECK-DAG: error: {{.*}}.o:(.text+0x0): relocation R_RX_DIR3U_PCREL out of range: 32 is not in [3, 10]
  bra.s near
# CHECK-DAG: error: {{.*}}.o:(.text+0x2): relocation R_RX_DIR8S_PCREL out of range: 511 is not in [-128, 127]
  beq.b ext
# CHECK-DAG: error: {{.*}}.o:(.text+0x4): relocation R_RX_DIR24S_PCREL out of range: 9433085 is not in [-8388608, 8388607]
  bsr.a far

  .data
# CHECK-DAG: error: {{.*}}.o:(.data+0x0): relocation R_RX_DIR16 out of range: 9437184 is not in [-32768, 65535]
  .short far
