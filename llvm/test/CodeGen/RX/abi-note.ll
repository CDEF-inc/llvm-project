; RUN: split-file %s %t
; RUN: llc -mtriple=rx-none-elf < %t/def.ll | FileCheck %s --check-prefix=CCRX
; RUN: llc -mtriple=rx-none-elf -target-abi=gnu < %t/def.ll | FileCheck %s --check-prefix=GNU
; RUN: llc -mtriple=rx-none-elf < %t/call.ll | FileCheck %s --check-prefix=CCRX
; RUN: llc -mtriple=rx-none-elf -target-abi=gnu < %t/call.ll | FileCheck %s --check-prefix=GNU
; RUN: llc -mtriple=rx-none-elf < %t/indirect.ll | FileCheck %s --check-prefix=CCRX
; RUN: llc -mtriple=rx-none-elf < %t/escaping.ll | FileCheck %s --check-prefix=CCRX
; RUN: llc -mtriple=rx-none-elf < %t/neutral.ll | FileCheck %s --check-prefix=NEUTRAL
; RUN: llc -mtriple=rx-none-elf -target-abi=gnu < %t/neutral.ll | FileCheck %s --check-prefix=NEUTRAL
; RUN: llc -mtriple=rx-none-elf < %t/empty.ll | FileCheck %s --check-prefix=NEUTRAL
; RUN: llc -mtriple=rx-none-elf -filetype=obj < %t/def.ll -o %t/def.o
; RUN: llvm-readelf -S -n %t/def.o | FileCheck %s --check-prefix=OBJ

; Every object records the calling convention it depends on in an ELF note
; (.note.rx.abi, owner "RX", type 1): 1 for CC-RX's (the default), 2 for
; GNURX's (-target-abi=gnu), 3 if no function it defines or calls outside
; the module places its arguments differently under the two. lld refuses to
; link 1 with 2 (lld/test/ELF/rx-abi-note.s).

; CCRX:      .section .note.rx.abi,"",@note
; CCRX-NEXT: .p2align 2, 0x0
; CCRX-NEXT: .long 3
; CCRX-NEXT: .long 8
; CCRX-NEXT: .long 1
; CCRX-NEXT: .asciz "RX"
; CCRX-NEXT: .p2align 2, 0x0
; CCRX-NEXT: .long 1
; CCRX-NEXT: .long 1

; GNU:      .section .note.rx.abi,"",@note
; GNU:      .asciz "RX"
; GNU-NEXT: .p2align 2, 0x0
; GNU-NEXT: .long 2
; GNU-NEXT: .long 1

; NEUTRAL:      .section .note.rx.abi,"",@note
; NEUTRAL:      .asciz "RX"
; NEUTRAL-NEXT: .p2align 2, 0x0
; NEUTRAL-NEXT: .long 3
; NEUTRAL-NEXT: .long 1

; OBJ: .note.rx.abi NOTE 00000000 {{[0-9a-f]+}} 000018 00 0 0 4
; OBJ: Displaying notes found in: .note.rx.abi
; OBJ:   RX 0x00000008 NT_VERSION (version)
; OBJ:    description data: 01 00 00 00 01 00 00 00

;--- def.ll
; A structure that is always on the stack and an int after it: the int is in
; R1 for CC-RX, in R3 for GNURX.
%s6 = type { [3 x i16] }
define i32 @after_stack(ptr byval(%s6) align 2 %s, i32 %x) {
  ret i32 %x
}

;--- call.ll
; A call whose last int follows an i64 that did not fit: R4 for CC-RX, the
; stack for GNURX.
declare i32 @g(i32, i32, i32, i64, i32)
define i32 @f() {
  %r = call i32 @g(i32 1, i32 2, i32 3, i64 4, i32 5)
  ret i32 %r
}

;--- indirect.ll
define i32 @f(ptr %p) {
  %r = call i32 %p(i32 1, i32 2, i32 3, i64 4, i32 5)
  ret i32 %r
}

;--- escaping.ll
; A function with local linkage whose address goes to another object, which
; may call it: it counts.
declare void @register(ptr)
define internal i32 @callback(i32 %a, i32 %b, i32 %c, i64 %d, i32 %e) {
  ret i32 %e
}
define void @f() {
  call void @register(ptr @callback)
  ret void
}

;--- neutral.ll
; Only arguments that both conventions place alike, and a function that does
; not, but has local linkage and is called only here (its address is not
; taken).
define internal i32 @local(i32 %a, i32 %b, i32 %c, i64 %d, i32 %e) noinline {
  ret i32 %e
}
declare i64 @h(i64, i64)
define i32 @f(i32 %a, i64 %b) {
  %r = call i32 @local(i32 1, i32 2, i32 3, i64 4, i32 5)
  %s = call i64 @h(i64 %b, i64 %b)
  ret i32 %r
}

;--- empty.ll
@x = global i32 1
