; RUN: llvm-mc -triple=rx-none-elf -mcpu=rxv3 -filetype=obj %s -o %t.o
; RUN: llvm-objdump -d -r %t.o | FileCheck %s
; RUN: llvm-readelf -h %t.o | FileCheck %s --check-prefix=FLAGS

; String instructions. GNU as marks
; an object that uses them in e_flags (SINSNS_SET | SINSNS_YES).
	scmpu
	smovu
	smovb
	smovf
	sstr.b
	sstr.w
	sstr.l
	suntil.b
	suntil.w
	suntil.l
	swhile.b
	swhile.w
	swhile.l
	rmpa.b
	rmpa.w
	rmpa.l

; The .text bytes, relocations and e_flags match GNU as 2.43 (rx-elf).
; CHECK-LABEL: 00000000 <.text>:
; CHECK-NEXT: 0: 7f 83 scmpu
; CHECK-NEXT: 2: 7f 87 smovu
; CHECK-NEXT: 4: 7f 8b smovb
; CHECK-NEXT: 6: 7f 8f smovf
; CHECK-NEXT: 8: 7f 88 sstr.b
; CHECK-NEXT: a: 7f 89 sstr.w
; CHECK-NEXT: c: 7f 8a sstr.l
; CHECK-NEXT: e: 7f 80 suntil.b
; CHECK-NEXT: 10: 7f 81 suntil.w
; CHECK-NEXT: 12: 7f 82 suntil.l
; CHECK-NEXT: 14: 7f 84 swhile.b
; CHECK-NEXT: 16: 7f 85 swhile.w
; CHECK-NEXT: 18: 7f 86 swhile.l
; CHECK-NEXT: 1a: 7f 8c rmpa.b
; CHECK-NEXT: 1c: 7f 8d rmpa.w
; CHECK-NEXT: 1e: 7f 8e rmpa.l
; FLAGS: Flags: 0x2c8
