# RUN: llvm-mc -triple=rx-none-elf -mcpu=rxv3 -show-encoding %s \
# RUN:   | FileCheck %s
# RUN: llvm-mc -triple=rx-none-elf -mcpu=rxv3e -show-encoding %s \
# RUN:   | FileCheck %s

# One instruction per line.
# Values sit on the boundaries where GNU as switches encodings.
# The instructions that need rxv3.
# The encodings match GNU as 2.43 (rx-elf).
xor r1, r2, r3
# CHECK: xor r1, r2, r3 ; encoding: [0xff,0x63,0x12]
