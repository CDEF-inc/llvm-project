# REQUIRES: rx
## LLVM records in each object the calling convention it depends on, as an
## ELF note in .note.rx.abi (llvm/test/CodeGen/RX/abi-note.ll): 1 CC-RX's,
## 2 GNURX's, 3 neither. Linking 1 with 2 is an error, and so is 1 with an
## object of an older LLVM (no note, a clang or rustc .comment), which
## followed GNURX's. Objects without the note otherwise (GCC's, CC-RX's,
## assembly) are not checked. -z rx-abi-check=warning|none relaxes it. The
## output has one note for all the inputs, counting an older LLVM's objects
## as GNURX's.

# RUN: rm -rf %t && split-file %s %t && cd %t
# RUN: llvm-mc -filetype=obj -triple=rx ccrx.s -o ccrx.o
# RUN: llvm-mc -filetype=obj -triple=rx gnu.s -o gnu.o
# RUN: llvm-mc -filetype=obj -triple=rx neutral.s -o neutral.o
# RUN: llvm-mc -filetype=obj -triple=rx old.s -o old.o
# RUN: llvm-mc -filetype=obj -triple=rx gcc.s -o gcc.o

## Agreeing, neutral and unchecked inputs link.
# RUN: ld.lld -e 0 ccrx.o neutral.o gcc.o -o out 2>&1 | count 0
# RUN: ld.lld -e 0 gnu.o neutral.o old.o gcc.o -o out 2>&1 | count 0
# RUN: ld.lld -e 0 old.o neutral.o -o out 2>&1 | count 0

# RUN: not ld.lld -e 0 ccrx.o gnu.o -o out 2>&1 | FileCheck %s --check-prefix=MIX
# MIX: error: ccrx.o uses CC-RX's calling convention (-mabi=ccrx) and gnu.o GNURX's (-mabi=gnu): calls between them may pass arguments in different places; build them with the same convention (-z rx-abi-check=warning to link anyway)

# RUN: not ld.lld -e 0 old.o ccrx.o -o out 2>&1 | FileCheck %s --check-prefix=OLD
# OLD: error: old.o was built by an older LLVM for RX (no ABI note, toolchain-2026-11-12 or older), which follows GNURX's calling convention, and ccrx.o uses CC-RX's: calls between them may pass arguments in different places; rebuild old.o with this toolchain (-z rx-abi-check=warning to link anyway)

# RUN: ld.lld -e 0 -z rx-abi-check=warning ccrx.o gnu.o -o out 2>&1 | FileCheck %s --check-prefix=WARN
# WARN: warning: ccrx.o uses CC-RX's calling convention
# RUN: ld.lld -e 0 -z rx-abi-check=none ccrx.o gnu.o old.o -o out 2>&1 | count 0
# RUN: ld.lld -e 0 -z rx-abi-check=error ccrx.o neutral.o -o out 2>&1 | count 0

## The output has one note: the inputs' combined.
# RUN: ld.lld -e 0 ccrx.o neutral.o gcc.o -o out
# RUN: llvm-readelf -n out | FileCheck %s --check-prefix=OUT1
# OUT1:     Displaying notes found in: .note.rx.abi
# OUT1:       RX 0x00000008 NT_VERSION (version)
# OUT1-NEXT:   description data: 01 00 00 00 01 00 00 00
# OUT1-NOT: RX 0x00000008
# RUN: ld.lld -e 0 neutral.o gcc.o -o out
# RUN: llvm-readelf -n out | FileCheck %s --check-prefix=OUT3
# OUT3: description data: 03 00 00 00 01 00 00 00
## No note in, none out (an older LLVM's object stays told by .comment).
# RUN: ld.lld -r old.o gcc.o -o r0.o
# RUN: llvm-readelf -S r0.o | FileCheck %s --check-prefix=NONOTE
# NONOTE-NOT: .note.rx.abi
# RUN: not ld.lld -e 0 r0.o ccrx.o -o out 2>&1 | FileCheck %s --check-prefix=OLDR0
# OLDR0: error: r0.o was built by an older LLVM for RX

## ld -r: the result is checked as a whole later.
# RUN: ld.lld -r ccrx.o neutral.o -o r.o
# RUN: llvm-readelf -n r.o | FileCheck %s --check-prefix=OUT1
# RUN: not ld.lld -e 0 r.o gnu.o -o out 2>&1 | FileCheck %s --check-prefix=MIXR
# MIXR: error: r.o uses CC-RX's calling convention (-mabi=ccrx) and gnu.o GNURX's
## An older LLVM's object in an ld -r output with a noted one: the output
## says GNURX's, so that it still cannot be linked with CC-RX's.
# RUN: ld.lld -r old.o neutral.o -o r2.o
# RUN: llvm-readelf -n r2.o | FileCheck %s --check-prefix=OUT2
# OUT2: description data: 02 00 00 00 01 00 00 00
# RUN: not ld.lld -e 0 r2.o ccrx.o -o out 2>&1 | FileCheck %s --check-prefix=OLDR
# OLDR: error: ccrx.o uses CC-RX's calling convention (-mabi=ccrx) and r2.o GNURX's

# RUN: not ld.lld -e 0 -z rx-abi-check=maybe ccrx.o -o out 2>&1 | FileCheck %s --check-prefix=BAD
# BAD: error: unknown -z rx-abi-check= value: maybe

#--- ccrx.s
  .section .note.rx.abi,"",@note
  .p2align 2
  .long 3, 8, 1
  .asciz "RX"
  .p2align 2
  .long 1, 1

#--- gnu.s
  .section .note.rx.abi,"",@note
  .p2align 2
  .long 3, 8, 1
  .asciz "RX"
  .p2align 2
  .long 2, 1

#--- neutral.s
  .section .note.rx.abi,"",@note
  .p2align 2
  .long 3, 8, 1
  .asciz "RX"
  .p2align 2
  .long 3, 1

#--- old.s
## What an older clang or rustc wrote: no note.
  .section .comment,"MS",@progbits,1
  .asciz "clang version 23.1.4"

#--- gcc.s
  .section .comment,"MS",@progbits,1
  .asciz "GCC: (GNU) 14.2.0"
