; RUN: llc -mtriple=rx-none-elf -verify-machineinstrs < %s | FileCheck %s

; GCC (rx-elf) does not promote char and short arguments: on the stack each
; takes its own size at its own alignment, so e, f and h below share the
; first word and i is at offset 4. The callee extends the value itself, or
; trusts a zeroext / signext attribute.

; CHECK-LABEL: _callee:
; CHECK-DAG: movu.b 4[r0], [[E:r[0-9]+]]
; CHECK-DAG: movu.b 5[r0], [[F:r[0-9]+]]
; CHECK-DAG: mov.w 6[r0], [[H:r[0-9]+]]
; CHECK-DAG: mov.l 8[r0], [[I:r[0-9]+]]
define i32 @callee(i32 %a, i32 %b, i32 %c, i32 %d, i8 zeroext %e, i8 zeroext %f,
                   i16 signext %h, i32 %i) {
  %e32 = zext i8 %e to i32
  %f32 = zext i8 %f to i32
  %h32 = sext i16 %h to i32
  %f3 = mul i32 %f32, 3
  %h5 = mul i32 %h32, 5
  %i7 = mul i32 %i, 7
  %s1 = add i32 %e32, %f3
  %s2 = add i32 %s1, %h5
  %s3 = add i32 %s2, %i7
  ret i32 %s3
}

; Without the attributes the callee extends (here with the load).
; CHECK-LABEL: _callee_noext:
; CHECK: mov.b 4[r0], r1
; CHECK-NEXT: rts
define i32 @callee_noext(i32 %a, i32 %b, i32 %c, i32 %d, i8 %e) {
  %e32 = sext i8 %e to i32
  ret i32 %e32
}

declare i32 @ext(i32, i32, i32, i32, i8, i8, i16, i32)

; CHECK-LABEL: _caller:
; CHECK-DAG: mov.b {{r[0-9]+}}, [r0]
; CHECK-DAG: mov.b {{r[0-9]+}}, 1[r0]
; CHECK-DAG: mov.w {{r[0-9]+}}, 2[r0]
; CHECK-DAG: mov.l {{r[0-9]+}}, 4[r0]
; CHECK: bsr.a _ext
define i32 @caller(i32 %x) {
  %e = trunc i32 %x to i8
  %x1 = add i32 %x, 1
  %f = trunc i32 %x1 to i8
  %x2 = add i32 %x, 2
  %h = trunc i32 %x2 to i16
  %x3 = add i32 %x, 3
  %r = call i32 @ext(i32 1, i32 2, i32 3, i32 4, i8 %e, i8 %f, i16 %h, i32 %x3)
  ret i32 %r
}

; In a variadic function the last named argument is on the stack, a char
; in one byte, and the unnamed ones follow at their own alignment.
declare i32 @vext(i8, ...)

; CHECK-LABEL: _vcaller:
; CHECK-DAG: mov.b #7, {{(0)?}}[r0]
; CHECK-DAG: mov.l #9, 4[r0]
define i32 @vcaller() {
  %r = call i32 (i8, ...) @vext(i8 7, i32 9)
  ret i32 %r
}
