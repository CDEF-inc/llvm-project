; RUN: llvm-mc -triple=rx-none-elf -mcpu=rxv3 -filetype=obj %s -o %t.o
; RUN: llvm-objdump -d -r %t.o | FileCheck %s
; RUN: llvm-readelf -h %t.o | FileCheck %s --check-prefix=FLAGS

; Symbolic immediates and data.
	.global	f
f:
	mov.l	#ext, r1
	mov.l	#ext+4, r15
	add	#ext, r2, r3
	cmp	#ext, r4
	mov.l	#ext, [r9]
	mov.l	#ext+4, 8[r2]
	rts
	.long	ext
	.long	ext+8

; The .text bytes, relocations and e_flags match GNU as 2.43 (rx-elf).
; CHECK-LABEL: 00000000 <f>:
; CHECK-NEXT: 0: fb 12 00 00 00 00 mov.l #0, r1
; CHECK-NEXT: 00000002: R_RX_DIR32 ext
; CHECK-NEXT: 6: fb f2 00 00 00 00 mov.l #0, r15
; CHECK-NEXT: 00000008: R_RX_DIR32 ext+0x4
; CHECK-NEXT: c: 70 23 00 00 00 00 add #0, r2, r3
; CHECK-NEXT: 0000000e: R_RX_DIR32 ext
; CHECK-NEXT: 12: 74 04 00 00 00 00 cmp #0, r4
; CHECK-NEXT: 00000014: R_RX_DIR32 ext
; CHECK-NEXT: 18: f8 92 00 00 00 00 mov.l #0, [r9]
; CHECK-NEXT: 0000001a: R_RX_DIR32 ext
; CHECK-NEXT: 1e: f9 22 02 00 00 00 00 mov.l #0, 8[r2]
; CHECK-NEXT: 00000021: R_RX_DIR32 ext+0x4
; CHECK-NEXT: 25: 02 rts
; CHECK-NEXT: 26: 00 brk
; CHECK-NEXT: 00000026: R_RX_DIR32 ext
; CHECK-NEXT: 27: 00 brk
; CHECK-NEXT: 28: 00 brk
; CHECK-NEXT: 29: 00 brk
; CHECK-NEXT: 2a: 00 brk
; CHECK-NEXT: 0000002a: R_RX_DIR32 ext+0x8
; CHECK-NEXT: 2b: 00 brk
; CHECK-NEXT: 2c: 00 brk
; CHECK-NEXT: 2d: 00 brk
; FLAGS: Flags: 0x208
