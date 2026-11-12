; RUN: llc -mtriple=rx-none-elf -O2 -verify-machineinstrs < %s | FileCheck %s

; Vector operations are split into scalars, but the DAG combiner and the
; type legalizer ask for the result type of a vector compare first
; (getSetCCResultType), which asserted. From llvm-test-suite's
; SingleSource/UnitTests/Vector/divides.c and matrix-types-spec.cpp.

; A <4 x i32> udiv: the combiner checks the divisor for one with a setcc.
; CHECK-LABEL: _udiv4:
; CHECK-COUNT-4: divu
; CHECK: rts
define void @udiv4(ptr %a, ptr %b, ptr %r) {
  %x = load <4 x i32>, ptr %a, align 16
  %y = load <4 x i32>, ptr %b, align 16
  %q = udiv <4 x i32> %x, %y
  store <4 x i32> %q, ptr %r, align 16
  ret void
}

; A compare and select of vectors.
; CHECK-LABEL: _umin4:
; CHECK-COUNT-4: cmp
; CHECK: rts
define void @umin4(ptr %a, ptr %b, ptr %r) {
  %x = load <4 x i32>, ptr %a, align 16
  %y = load <4 x i32>, ptr %b, align 16
  %c = icmp ult <4 x i32> %x, %y
  %m = select <4 x i1> %c, <4 x i32> %x, <4 x i32> %y
  store <4 x i32> %m, ptr %r, align 16
  ret void
}

; A <3 x double> load is widened, which also asks for the compare type.
; CHECK-LABEL: _copy3:
; CHECK: rts
define void @copy3(ptr %a, ptr %r) {
  %x = load <3 x double>, ptr %a, align 8
  %y = fadd <3 x double> %x, %x
  store <3 x double> %y, ptr %r, align 8
  ret void
}
