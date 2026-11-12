; RUN: llc -mtriple=rx-none-elf -O2 -verify-machineinstrs < %s | FileCheck %s

; A stack slot beyond dsp:16 (64 K accesses up from SP) is reached through
; a scratch register: add #offset, r0, rN and the access at [rN]. It was a
; fatal error ("stack offset not encodable in a dsp:16 access"), first seen
; in llvm-test-suite's SingleSource/UnitTests/matrix-types-spec.cpp (a
; 268 KB frame).

declare void @use(ptr)

; %e and %f are on the stack above the 300000-byte buffer: out of reach of
; mov.l's dsp:16 (4 * 65535 bytes) and of movu.b's (65535 bytes).
; CHECK-LABEL: _far:
; CHECK: add #-300004, r0, r0
; CHECK: bsr.a _use
; CHECK-DAG: add #300012, r0, [[F:r[0-9]+]]
; CHECK-DAG: movu.b [[[F]]], {{r[0-9]+}}
; CHECK-DAG: add #300008, r0, [[E:r[0-9]+]]
; CHECK-DAG: mov.l [[[E]]], {{r[0-9]+}}
; CHECK: add #300004, r0, r0
define i32 @far(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e, i8 %f) {
  %buf = alloca [300000 x i8], align 4
  call void @use(ptr %buf)
  %f32 = zext i8 %f to i32
  %r = add i32 %e, %f32
  ret i32 %r
}

; In reach: the accesses keep their displacement.
; CHECK-LABEL: _near:
; CHECK: add #-8000, r0, r0
; CHECK-DAG: movu.b 8008[r0], {{r[0-9]+}}
; CHECK-DAG: mov.l 8004[r0], {{r[0-9]+}}
define i32 @near(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e, i8 %f) {
  %buf = alloca [8000 x i8], align 4
  call void @use(ptr %buf)
  %f32 = zext i8 %f to i32
  %r = add i32 %e, %f32
  ret i32 %r
}
