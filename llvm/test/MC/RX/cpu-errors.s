# RUN: not llvm-mc -triple=rx-none-elf -mcpu=rxv1 %s -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefixes=V2,V3,V3E --implicit-check-not=error:
# RUN: not llvm-mc -triple=rx-none-elf -mcpu=rxv2 %s -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefixes=V3,V3E --implicit-check-not=error:
# RUN: not llvm-mc -triple=rx-none-elf -mcpu=rxv3 %s -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=V3E --implicit-check-not=error:
# RUN: llvm-mc -triple=rx-none-elf -mcpu=rxv3e %s -o /dev/null

# Instructions that a CPU does not have are rejected. V2 / V3 / V3E mark the
# instructions that need RXv2 / RXv3 / RXv3e; the last RUN line checks that
# RXv3e accepts all of them. The error names the first CPU that has the
# instruction (the DFPU comes with RXv3).

# RXv2
stz r1, r2
# V2: :[[@LINE-1]]:1: error: instruction requires a CPU feature not currently enabled (-mcpu=rxv2 or later)
movli [r1], r2
# V2: :[[@LINE-1]]:1: error: instruction requires a CPU feature not currently enabled (-mcpu=rxv2 or later)
movco r3, [r4]
# V2: :[[@LINE-1]]:1: error: instruction requires a CPU feature not currently enabled (-mcpu=rxv2 or later)
mulhi r1, r2, a1
# V2: :[[@LINE-1]]:1: error: instruction requires a CPU feature not currently enabled (-mcpu=rxv2 or later)
emaca r15, r0, a1
# V2: :[[@LINE-1]]:1: error: instruction requires a CPU feature not currently enabled (-mcpu=rxv2 or later)
fadd r1, r2, r3
# V2: :[[@LINE-1]]:1: error: instruction requires a CPU feature not currently enabled (-mcpu=rxv2 or later)
fsqrt r1, r2
# V2: :[[@LINE-1]]:1: error: instruction requires a CPU feature not currently enabled (-mcpu=rxv2 or later)
utof r1, r2
# V2: :[[@LINE-1]]:1: error: instruction requires a CPU feature not currently enabled (-mcpu=rxv2 or later)

# RXv3
xor r1, r2, r3
# V3: :[[@LINE-1]]:1: error: instruction requires a CPU feature not currently enabled (-mcpu=rxv3 or later)
bfmov #1, #2, #3, r1, r2
# V3: :[[@LINE-1]]:1: error: instruction requires a CPU feature not currently enabled (-mcpu=rxv3 or later)
rstr #1
# V3: :[[@LINE-1]]:1: error: instruction requires a CPU feature not currently enabled (-mcpu=rxv3 or later)
dadd dr1, dr2, dr3
# V3: :[[@LINE-1]]:1: error: instruction requires a CPU feature not currently enabled (-mcpu=rxv3 or later)
dmov.d dr1, 4[r2]
# V3: :[[@LINE-1]]:1: error: instruction requires a CPU feature not currently enabled (-mcpu=rxv3 or later)

# RXv3e
trcall #0
# V3E: :[[@LINE-1]]:1: error: instruction requires a CPU feature not currently enabled (-mcpu=rxv3e or later)
trrte
# V3E: :[[@LINE-1]]:1: error: instruction requires a CPU feature not currently enabled (-mcpu=rxv3e or later)
int r0
# V3E: :[[@LINE-1]]:1: error: instruction requires a CPU feature not currently enabled (-mcpu=rxv3e or later)
mvtipl #16
# V3E: :[[@LINE-1]]:1: error: instruction requires a CPU feature not currently enabled (-mcpu=rxv3e or later)
# The RXv3e control registers are not registers before RXv3e.
mvtc r1, ivc
# V3E: :[[@LINE-1]]:10: error: invalid operand for instruction
pushc wid
# V3E: :[[@LINE-1]]:7: error: invalid operand for instruction
