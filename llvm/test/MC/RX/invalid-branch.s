# RUN: not llvm-mc -triple=rx-none-elf -filetype=obj %s -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --implicit-check-not=error:

# Branches to a label that is out of the form's range (the constants are in
# invalid.s). bra.s / beq.s / bne.s reach 3..10 bytes ahead of the branch;
# these are -3, 15, 1, 0 and 11 bytes away.

back:
	nop
	nop
	nop
	bra.s	back
# CHECK: :[[@LINE-1]]:8: error: branch target out of range for .S (3 to 10 bytes)
	beq.s	1f
# CHECK: :[[@LINE-1]]:8: error: branch target out of range for .S (3 to 10 bytes)
	bne.s	self
# CHECK: :[[@LINE-1]]:8: error: branch target out of range for .S (3 to 10 bytes)
self:
	bne.s	self
# CHECK: :[[@LINE-1]]:8: error: branch target out of range for .S (3 to 10 bytes)
	bra.s	2f
# CHECK: :[[@LINE-1]]:8: error: branch target out of range for .S (3 to 10 bytes)
	.space	10
2:
	.space	1
1:
	bra.b	3f
# CHECK: :[[@LINE-1]]:8: error: branch target out of range
	.space	128
3:
	bra.w	4f
# CHECK: :[[@LINE-1]]:8: error: branch target out of range
	.space	32768
4:
