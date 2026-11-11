; RUN: llvm-mc -triple=rx-none-elf -mcpu=rxv3 -filetype=obj %s -o %t.o
; RUN: llvm-objdump -d -r %t.o | FileCheck %s
; RUN: llvm-readelf -h %t.o | FileCheck %s --check-prefix=FLAGS

; Branches to local labels (resolved by the assembler) and to an undefined
; symbol (relocations).
	.global	start
start:
	nop
	bra.s	.Ls1		; 3..10 bytes ahead
	nop
	nop
.Ls1:
	beq.s	.Ls2
	nop
	nop
	nop
	nop
	nop
	nop
	nop
.Ls2:
	bne.s	.Ls3
	nop
	nop
.Ls3:
	bra.b	start		; backward
	bra.b	.Lfwd
	beq.b	.Lfwd
	bne.b	.Lfwd
	bgeu.b	.Lfwd
	bltu.b	.Lfwd
	bgtu.b	.Lfwd
	bleu.b	.Lfwd
	bpz.b	.Lfwd
	bn.b	.Lfwd
	bge.b	.Lfwd
	blt.b	.Lfwd
	bgt.b	.Lfwd
	ble.b	.Lfwd
	bo.b	.Lfwd
	bno.b	.Lfwd
	bc.b	.Lfwd
	bnc.b	.Lfwd
	bz.b	.Lfwd
	bnz.b	.Lfwd
	bra.w	start
	bra.w	.Lfwd
	beq.w	.Lfwd
	bne.w	.Lfwd
	bsr.w	start
	bra.a	start
	bra.a	.Lfwd
	bsr.a	.Lfwd
.Lfwd:
	rts

	; References to an undefined symbol.
	bra.a	ext
	bsr.a	ext
	bra.w	ext
	bsr.w	ext
	bra.b	ext
	beq.b	ext
	rts

; The .text bytes, relocations and e_flags match GNU as 2.43 (rx-elf).
; CHECK-LABEL: 00000000 <start>:
; CHECK: 1: 0b bra.s 0x4 <start+0x4>
; CHECK: 4: 10 beq.s 0xc <start+0xc>
; CHECK: c: 1b bne.s 0xf <start+0xf>
; CHECK: f: 2e 00 bra.b 0xf <start+0xf>
; CHECK-NEXT: 00000010: R_RX_DIR8S_PCREL start
; CHECK-NEXT: 11: 2e 41 bra.b 0x52 <start+0x52>
; CHECK-NEXT: 13: 20 3f beq.b 0x52 <start+0x52>
; CHECK-NEXT: 15: 21 3d bne.b 0x52 <start+0x52>
; CHECK-NEXT: 17: 22 3b bgeu.b 0x52 <start+0x52>
; CHECK-NEXT: 19: 23 39 bltu.b 0x52 <start+0x52>
; CHECK-NEXT: 1b: 24 37 bgtu.b 0x52 <start+0x52>
; CHECK-NEXT: 1d: 25 35 bleu.b 0x52 <start+0x52>
; CHECK-NEXT: 1f: 26 33 bpz.b 0x52 <start+0x52>
; CHECK-NEXT: 21: 27 31 bn.b 0x52 <start+0x52>
; CHECK-NEXT: 23: 28 2f bge.b 0x52 <start+0x52>
; CHECK-NEXT: 25: 29 2d blt.b 0x52 <start+0x52>
; CHECK-NEXT: 27: 2a 2b bgt.b 0x52 <start+0x52>
; CHECK-NEXT: 29: 2b 29 ble.b 0x52 <start+0x52>
; CHECK-NEXT: 2b: 2c 27 bo.b 0x52 <start+0x52>
; CHECK-NEXT: 2d: 2d 25 bno.b 0x52 <start+0x52>
; CHECK-NEXT: 2f: 22 23 bgeu.b 0x52 <start+0x52>
; CHECK-NEXT: 31: 23 21 bltu.b 0x52 <start+0x52>
; CHECK-NEXT: 33: 20 1f beq.b 0x52 <start+0x52>
; CHECK-NEXT: 35: 21 1d bne.b 0x52 <start+0x52>
; CHECK-NEXT: 37: 38 00 00 bra.w 0x37 <start+0x37>
; CHECK-NEXT: 00000038: R_RX_DIR16S_PCREL start
; CHECK-NEXT: 3a: 38 18 00 bra.w 0x52 <start+0x52>
; CHECK-NEXT: 3d: 3a 15 00 beq.w 0x52 <start+0x52>
; CHECK-NEXT: 40: 3b 12 00 bne.w 0x52 <start+0x52>
; CHECK-NEXT: 43: 39 00 00 bsr.w 0x43 <start+0x43>
; CHECK-NEXT: 00000044: R_RX_DIR16S_PCREL start
; CHECK-NEXT: 46: 04 00 00 00 bra.a 0x46 <start+0x46>
; CHECK-NEXT: 00000047: R_RX_DIR24S_PCREL start
; CHECK-NEXT: 4a: 04 08 00 00 bra.a 0x52 <start+0x52>
; CHECK-NEXT: 4e: 05 04 00 00 bsr.a 0x52 <start+0x52>
; CHECK-NEXT: 52: 02 rts
; CHECK-NEXT: 53: 04 00 00 00 bra.a 0x53 <start+0x53>
; CHECK-NEXT: 00000054: R_RX_DIR24S_PCREL ext
; CHECK-NEXT: 57: 05 00 00 00 bsr.a 0x57 <start+0x57>
; CHECK-NEXT: 00000058: R_RX_DIR24S_PCREL ext
; CHECK-NEXT: 5b: 38 00 00 bra.w 0x5b <start+0x5b>
; CHECK-NEXT: 0000005c: R_RX_DIR16S_PCREL ext
; CHECK-NEXT: 5e: 39 00 00 bsr.w 0x5e <start+0x5e>
; CHECK-NEXT: 0000005f: R_RX_DIR16S_PCREL ext
; CHECK-NEXT: 61: 2e 00 bra.b 0x61 <start+0x61>
; CHECK-NEXT: 00000062: R_RX_DIR8S_PCREL ext
; CHECK-NEXT: 63: 20 00 beq.b 0x63 <start+0x63>
; CHECK-NEXT: 00000064: R_RX_DIR8S_PCREL ext
; CHECK-NEXT: 65: 02 rts
; FLAGS: Flags: 0x208
