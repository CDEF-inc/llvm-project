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
