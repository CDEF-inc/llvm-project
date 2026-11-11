; RUN: llvm-mc -triple=rx-none-elf -mcpu=rxv3 -filetype=obj %s -o %t.o
; RUN: llvm-objdump -d -r %t.o | FileCheck %s
; RUN: llvm-readelf -h %t.o | FileCheck %s --check-prefix=FLAGS

; GNU as syntax without a size: mov / movu / push / sstr / suntil / swhile /
; rmpa take their default size, bra / bsr Rn are .L, and bra / bsr /
; b<cond> to a label get the form the distance needs (bra.s / .b / .w / .a,
; bsr.w / .a, beq / bne .s / .b / .w, b<cond>.b, and a b<!cond> around a
; bra.w or bra.a when b<cond> cannot reach). An immediate A - B with B
; undefined becomes the R_RX_SYM / R_RX_OPsub / R_RX_ABS32 relocations.
	.global	start, g
start:
	mov	r1, r2
	mov	#1, r2
	mov	#300, r2
	mov	4[r1], r2
	mov	r1, [r2]
	mov	[r1+], r2
	mov	r1, [-r2]
	mov	[r1, r2], r3
	mov	[r1], [r2]
	movu	[r1], r2
	movu	2[r1], r2
	push	r1
	sstr
	suntil
	swhile
	rmpa
	bra	r1
	bsr	r1

; Forward and backward branches at the edges of each form.
	bra	1f
1:	bra	2f
	.space	8, 3
2:	bra	3f
	.space	9, 3
3:	bra	4f
	.space	125, 3
4:	bra	5f
	.space	126, 3
5:	bra	6f
	.space	32764, 3
6:	bra	7f
	.space	32765, 3
7:	bra	6b
	bra	5b
	beq	8f
	.space	8, 3
8:	bne	9f
	.space	9, 3
9:	bz	10f
	.space	125, 3
10:	bnz	11f
	.space	126, 3
11:	beq	12f
	.space	32764, 3
12:	bne	13f
	.space	32765, 3
13:	bgt	14f
	.space	125, 3
14:	bc	15f
	.space	126, 3
15:	bgeu	16f
	.space	32762, 3
16:	ble	17f
	.space	32763, 3
17:	bnc	12b
	bpz	5b
	bn	1b
	bsr	18f
	.space	32764, 3
18:	bsr	19f
	.space	32765, 3
19:	bsr	start

; Targets the assembler cannot resolve.
	bra	far
	bsr	far
	beq	far
	bne	far
	bgtu	far
	bleu	far
	bge	far
	blt	far
	bo	far
	bno	far
	bra	g
	bgt	g
	nop
g:	nop

; Differences of symbols that are not defined here.
	mov	#(_e - _s), r3
	mov.l	#(_e - _s + 4), r3
	add	#(_e - _s), r3
	cmp	#(_e - _s), r3
	mov.l	#(_e - _s), [r1]
	mov	#(_e - here), r3
here:	nop
	.long	_e - _s

; The .text bytes, relocations and e_flags match GNU as 2.43 (rx-elf).
; CHECK-LABEL: 00000000 <start>:
; CHECK-NEXT: 0: ef 12 mov.l r1, r2
; CHECK-NEXT: 2: 66 12 mov.l #1, r2
; CHECK-NEXT: 4: fb 2a 2c 01 mov.l #300, r2
; CHECK-NEXT: 8: a8 1a mov.l 4[r1], r2
; CHECK-NEXT: a: e3 21 mov.l r1, [r2]
; CHECK-NEXT: c: fd 2a 12 mov.l [r1+], r2
; CHECK-NEXT: f: fd 26 21 mov.l r1, [-r2]
; CHECK-NEXT: 12: fe 61 23 mov.l [r1, r2], r3
; CHECK-NEXT: 15: e0 12 mov.l [r1], [r2]
; CHECK-NEXT: 17: 5c 12 movu.w [r1], r2
; CHECK-NEXT: 19: b8 1a movu.w 2[r1], r2
; CHECK-NEXT: 1b: 7e a1 push.l r1
; CHECK-NEXT: 1d: 7f 8a sstr.l
; CHECK-NEXT: 1f: 7f 82 suntil.l
; CHECK-NEXT: 21: 7f 86 swhile.l
; CHECK-NEXT: 23: 7f 8e rmpa.l
; CHECK-NEXT: 25: 7f 41 bra.l r1
; CHECK-NEXT: 27: 7f 51 bsr.l r1
; CHECK-NEXT: 29: 2e 02 bra.b 0x2b <start+0x2b>
; CHECK-NEXT: 2b: 09 bra.s 0x34 <start+0x34>
; CHECK: 34: 0a bra.s 0x3e <start+0x3e>
; CHECK: 3e: 2e 7f bra.b 0xbd <start+0xbd>
; CHECK: bd: 38 81 00 bra.w 0x13e <start+0x13e>
; CHECK: 13e: 38 ff 7f bra.w 0x813d <start+0x813d>
; CHECK: 813d: 04 01 80 00 bra.a 0x1013e <start+0x1013e>
; CHECK: 1013e: 04 ff 7f ff bra.a 0x813d <start+0x813d>
; CHECK-NEXT: 10142: 04 fc ff fe bra.a 0x13e <start+0x13e>
; CHECK-NEXT: 10146: 11 beq.s 0x1014f <start+0x1014f>
; CHECK: 1014f: 1a bne.s 0x10159 <start+0x10159>
; CHECK: 10159: 20 7f beq.b 0x101d8 <start+0x101d8>
; CHECK: 101d8: 3b 81 00 bne.w 0x10259 <start+0x10259>
; CHECK: 10259: 3a ff 7f beq.w 0x18258 <start+0x18258>
; CHECK: 18258: 15 beq.s 0x1825d <start+0x1825d>
; CHECK-NEXT: 18259: 04 01 80 00 bra.a 0x2025a <start+0x2025a>
; CHECK: 2025a: 2a 7f bgt.b 0x202d9 <start+0x202d9>
; CHECK: 202d9: 23 05 bltu.b 0x202de <start+0x202de>
; CHECK-NEXT: 202db: 38 81 00 bra.w 0x2035c <start+0x2035c>
; CHECK: 2035c: 23 05 bltu.b 0x20361 <start+0x20361>
; CHECK-NEXT: 2035e: 38 fd 7f bra.w 0x2835b <start+0x2835b>
; CHECK: 2835b: 2a 06 bgt.b 0x28361 <start+0x28361>
; CHECK-NEXT: 2835d: 04 ff 7f 00 bra.a 0x3035c <start+0x3035c>
; CHECK: 3035c: 22 06 bgeu.b 0x30362 <start+0x30362>
; CHECK-NEXT: 3035e: 04 fa 7e fe bra.a 0x18258 <start+0x18258>
; CHECK-NEXT: 30362: 27 06 bn.b 0x30368 <start+0x30368>
; CHECK-NEXT: 30364: 04 da fd fc bra.a 0x13e <start+0x13e>
; CHECK-NEXT: 30368: 26 06 bpz.b 0x3036e <start+0x3036e>
; CHECK-NEXT: 3036a: 04 c1 fc fc bra.a 0x2b <start+0x2b>
; CHECK-NEXT: 3036e: 39 ff 7f bsr.w 0x3836d <start+0x3836d>
; CHECK: 3836d: 05 01 80 00 bsr.a 0x4036e <start+0x4036e>
; CHECK: 4036e: 05 00 00 00 bsr.a 0x4036e <start+0x4036e>
; CHECK-NEXT: 0004036f: R_RX_DIR24S_PCREL start
; CHECK-NEXT: 40372: 04 00 00 00 bra.a 0x40372 <start+0x40372>
; CHECK-NEXT: 00040373: R_RX_DIR24S_PCREL far
; CHECK-NEXT: 40376: 05 00 00 00 bsr.a 0x40376 <start+0x40376>
; CHECK-NEXT: 00040377: R_RX_DIR24S_PCREL far
; CHECK-NEXT: 4037a: 3a 00 00 beq.w 0x4037a <start+0x4037a>
; CHECK-NEXT: 0004037b: R_RX_DIR16S_PCREL far
; CHECK-NEXT: 4037d: 3b 00 00 bne.w 0x4037d <start+0x4037d>
; CHECK-NEXT: 0004037e: R_RX_DIR16S_PCREL far
; CHECK-NEXT: 40380: 24 00 bgtu.b 0x40380 <start+0x40380>
; CHECK-NEXT: 00040381: R_RX_DIR8S_PCREL far
; CHECK-NEXT: 40382: 25 00 bleu.b 0x40382 <start+0x40382>
; CHECK-NEXT: 00040383: R_RX_DIR8S_PCREL far
; CHECK-NEXT: 40384: 28 00 bge.b 0x40384 <start+0x40384>
; CHECK-NEXT: 00040385: R_RX_DIR8S_PCREL far
; CHECK-NEXT: 40386: 29 00 blt.b 0x40386 <start+0x40386>
; CHECK-NEXT: 00040387: R_RX_DIR8S_PCREL far
; CHECK-NEXT: 40388: 2c 00 bo.b 0x40388 <start+0x40388>
; CHECK-NEXT: 00040389: R_RX_DIR8S_PCREL far
; CHECK-NEXT: 4038a: 2d 00 bno.b 0x4038a <start+0x4038a>
; CHECK-NEXT: 0004038b: R_RX_DIR8S_PCREL far
; CHECK-NEXT: 4038c: 04 00 00 00 bra.a 0x4038c <start+0x4038c>
; CHECK-NEXT: 0004038d: R_RX_DIR24S_PCREL g
; CHECK-NEXT: 40390: 2a 00 bgt.b 0x40390 <start+0x40390>
; CHECK-NEXT: 00040391: R_RX_DIR8S_PCREL g
; CHECK-LABEL: 00040393 <g>:
; CHECK: 40394: fb 32 00 00 00 00 mov.l #0, r3
; CHECK-NEXT: 00040396: R_RX_SYM _e
; CHECK-NEXT: 00040396: R_RX_SYM _s
; CHECK-NEXT: 00040396: R_RX_OPsub _s
; CHECK-NEXT: 00040396: R_RX_ABS32 _s
; CHECK-NEXT: 4039a: fb 32 00 00 00 00 mov.l #0, r3
; CHECK-NEXT: 0004039c: R_RX_SYM _e+0x4
; CHECK-NEXT: 0004039c: R_RX_SYM _s
; CHECK-NEXT: 0004039c: R_RX_OPsub _s
; CHECK-NEXT: 0004039c: R_RX_ABS32 _s
; CHECK-NEXT: 403a0: 70 33 00 00 00 00 add #0, r3, r3
; CHECK-NEXT: 000403a2: R_RX_SYM _e
; CHECK-NEXT: 000403a2: R_RX_SYM _s
; CHECK-NEXT: 000403a2: R_RX_OPsub _s
; CHECK-NEXT: 000403a2: R_RX_ABS32 _s
; CHECK-NEXT: 403a6: 74 03 00 00 00 00 cmp #0, r3
; CHECK-NEXT: 000403a8: R_RX_SYM _e
; CHECK-NEXT: 000403a8: R_RX_SYM _s
; CHECK-NEXT: 000403a8: R_RX_OPsub _s
; CHECK-NEXT: 000403a8: R_RX_ABS32 _s
; CHECK-NEXT: 403ac: f8 12 00 00 00 00 mov.l #0, [r1]
; CHECK-NEXT: 000403ae: R_RX_SYM _e
; CHECK-NEXT: 000403ae: R_RX_SYM _s
; CHECK-NEXT: 000403ae: R_RX_OPsub _s
; CHECK-NEXT: 000403ae: R_RX_ABS32 _s
; CHECK-NEXT: 403b2: fb 32 00 00 00 00 mov.l #0, r3
; CHECK-NEXT: 000403b4: R_RX_SYM _e
; CHECK-NEXT: 000403b4: R_RX_SYM here
; CHECK-NEXT: 000403b4: R_RX_OPsub here
; CHECK-NEXT: 000403b4: R_RX_ABS32 here
; CHECK-LABEL: 000403b8 <here>:
; CHECK: 403b9: 00 brk
; CHECK-NEXT: 000403b9: R_RX_SYM _e
; CHECK-NEXT: 000403b9: R_RX_SYM _s
; CHECK-NEXT: 000403b9: R_RX_OPsub _s
; CHECK-NEXT: 000403b9: R_RX_ABS32 _s
; CHECK-NEXT: 403ba: 00 brk
; CHECK-NEXT: 403bb: 00 brk
; CHECK-NEXT: 403bc: 00 brk
; FLAGS: Flags: 0x2c8
