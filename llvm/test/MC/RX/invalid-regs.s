# RUN: not llvm-mc -triple=rx-none-elf -mcpu=rxv3e %s -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --implicit-check-not=error:
# RUN: llvm-mc -triple=rx-none-elf -mcpu=rxv3e -show-encoding --defsym=VALID=1 %s \
# RUN:   | FileCheck %s --check-prefix=VALID

# Register operands that the encoding can hold but the manual rules out; GNU
# as rejects the ranges too.

.ifndef VALID
popm r0-r3
# CHECK: :[[@LINE-1]]:1: error: R0 cannot be in the register range
popm r3-r2
# CHECK: :[[@LINE-1]]:1: error: the first register of the range must be below the last
pushm r0-r0
# CHECK: :[[@LINE-1]]:1: error: R0 cannot be in the register range
rtsd #0, r0-r3
# CHECK: :[[@LINE-1]]:1: error: R0 cannot be in the register range
rtsd #0, r3-r2
# CHECK: :[[@LINE-1]]:1: error: register range must be ascending
emul r1, r15
# CHECK: :[[@LINE-1]]:1: error: the result pair Rd:Rd+1 cannot start at R15
emulu #1, r15
# CHECK: :[[@LINE-1]]:1: error: the result pair Rd:Rd+1 cannot start at R15
emul [r1].l, r15
# CHECK: :[[@LINE-1]]:1: error: the result pair Rd:Rd+1 cannot start at R15
emulu 4[r2].w, r15
# CHECK: :[[@LINE-1]]:1: error: the result pair Rd:Rd+1 cannot start at R15
.else
# A one-register range is PUSH.L / POP, as in GNU as; RTSD keeps it.
pushm r5-r5
# VALID: push.l r5 ; encoding: [0x7e,0xa5]
popm r5-r5
# VALID: pop r5 ; encoding: [0x7e,0xb5]
rtsd #4, r6-r6
# VALID: rtsd #4, r6-r6 ; encoding: [0x3f,0x66,0x01]
emul r1, r14
# VALID: emul r1, r14
.endif
