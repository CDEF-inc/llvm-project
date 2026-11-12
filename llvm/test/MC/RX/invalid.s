# RUN: not llvm-mc -triple=rx-none-elf -mcpu=rxv3e %s -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --implicit-check-not=error:

# Operands out of range for every encoding of the instruction.

# The dmov.d displacement is scaled by 4 (the RX manual says 8, but the
# RX72M and CC-RX scale it by 4), so it must be a multiple of 4.
dmov.d dr3, 2[r4]
# CHECK: :[[@LINE-1]]:13: error: invalid operand for instruction
mvtipl #32
# CHECK: :[[@LINE-1]]:8: error: invalid operand for instruction
racw #3
# CHECK: :[[@LINE-1]]:6: error: invalid operand for instruction
add #1, r16
# CHECK: :[[@LINE-1]]:9: error: invalid operand for instruction
mov.b r1, [r2+r3]
# CHECK: :[[@LINE-1]]:15: error: expected ']'

# A constant branch displacement must fit the form: 3..10 for .s, signed
# 8 / 16 / 24 bits for .b / .w / .a (labels: invalid-branch.s).
bra.s 11
# CHECK: :[[@LINE-1]]:7: error: invalid operand for instruction
bra.s 2
# CHECK: :[[@LINE-1]]:7: error: invalid operand for instruction
bra.s -1
# CHECK: :[[@LINE-1]]:7: error: invalid operand for instruction
beq.s 11
# CHECK: :[[@LINE-1]]:7: error: invalid operand for instruction
bne.s 0
# CHECK: :[[@LINE-1]]:7: error: invalid operand for instruction
bz.s 11
# CHECK: :[[@LINE-1]]:6: error: invalid operand for instruction
bnz.s 2
# CHECK: :[[@LINE-1]]:7: error: invalid operand for instruction
bra.b 128
# CHECK: :[[@LINE-1]]:7: error: invalid operand for instruction
beq.b -129
# CHECK: :[[@LINE-1]]:7: error: invalid operand for instruction
bra.w 32768
# CHECK: :[[@LINE-1]]:7: error: invalid operand for instruction
bsr.w -32769
# CHECK: :[[@LINE-1]]:7: error: invalid operand for instruction
bra.a 8388608
# CHECK: :[[@LINE-1]]:7: error: invalid operand for instruction
bsr.a -8388609
# CHECK: :[[@LINE-1]]:7: error: invalid operand for instruction

# cr 1 (PC) is reserved for mvtc and popc; mvfc and pushc read it.
mvtc r1, pc
# CHECK: :[[@LINE-1]]:10: error: invalid operand for instruction
mvtc #1, pc
# CHECK: :[[@LINE-1]]:10: error: invalid operand for instruction
popc pc
# CHECK: :[[@LINE-1]]:6: error: invalid operand for instruction
