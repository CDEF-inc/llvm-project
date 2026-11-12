// The rx-none-elf toolchain: GCC's (rx-elf-gcc's) defaults and options,
// the integrated assembler, ld.lld, and an rx-elf GCC installation for
// libgcc, the C library and the multilib that matches the options.

// RUN: %clang -### --target=rx-none-elf -c %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CC1 %s
// CC1: "-cc1" "-triple" "rx-unknown-none-elf" "-emit-obj"
// CC1-SAME: "-nostdsysteminc" "-target-cpu" "rx600" "-target-feature" "+64bit-doubles"
// CC1-SAME: "-fno-signed-char"

// RUN: %clang -### --target=rx-none-elf -mcpu=rxv3 -mnofpu -mno-dfpu \
// RUN:   -m32bit-doubles -mrelax -c %s 2>&1 | FileCheck --check-prefix=FEAT %s
// FEAT: "-target-cpu" "rxv3" "-target-feature" "-fpu" "-target-feature" "-dfpu" "-target-feature" "-64bit-doubles" "-target-feature" "+relax"

// RUN: %clang -### --target=rx-none-elf -mcpu=rx66t -fpu -mdfpu -c %s 2>&1 \
// RUN:   | FileCheck --check-prefix=FEAT2 %s
// RUN: %clang -### --target=rx-none-elf -mcpu=rx66t -nofpu -fpu -mdfpu -c %s 2>&1 \
// RUN:   | FileCheck --check-prefix=FEAT2 %s
// FEAT2: "-target-cpu" "rx66t" "-target-feature" "+fpu" "-target-feature" "+dfpu" "-target-feature" "+64bit-doubles"

// GNURX's -misa=, when there is no -mcpu=: with the FPU, and with the DFPU
// only if -mdfpu (unlike -mcpu=rxv1 and -mcpu=rxv3).
// RUN: %clang -### --target=rx-none-elf -misa=v1 -c %s 2>&1 \
// RUN:   | FileCheck --check-prefix=ISA %s
// ISA: "-target-cpu" "rxv1" "-target-feature" "+fpu" "-target-feature" "-dfpu" "-target-feature" "+64bit-doubles"
// RUN: %clang -### --target=rx-none-elf -misa=v3 -mdfpu -nofpu -c %s 2>&1 \
// RUN:   | FileCheck --check-prefix=ISA3 %s
// ISA3: "-target-cpu" "rxv3" "-target-feature" "-fpu" "-target-feature" "+dfpu"
// RUN: %clang -### --target=rx-none-elf -misa=v3 -mcpu=rx610 -c %s 2>&1 \
// RUN:   | FileCheck --check-prefix=ISA-MCPU %s
// ISA-MCPU: "-target-cpu" "rx610"
// RUN: not %clang -### --target=rx-none-elf -misa=v4 -c %s 2>&1 \
// RUN:   | FileCheck --check-prefix=ISA-BAD %s
// ISA-BAD: error: unsupported argument 'v4' to option '-misa='
// RUN: not %clang -### --target=rx-none-elf -mcpu=rx100 -fpu -c %s 2>&1 \
// RUN:   | FileCheck --check-prefix=NOFPU-CPU %s
// NOFPU-CPU: error: invalid argument '-fpu' not allowed with '-mcpu=rx100'
// As GNURX: "DFPU is not supported for this ISA/CPU", "DFPU is not supported
// with 32 bit doubles".
// RUN: not %clang -### --target=rx-none-elf -mcpu=rx64m -mdfpu -c %s 2>&1 \
// RUN:   | FileCheck --check-prefix=DFPU-CPU %s
// DFPU-CPU: error: invalid argument '-mdfpu' not allowed with '-mcpu=rx64m'
// RUN: not %clang -### --target=rx-none-elf -mcpu=rxv3 -mdfpu -m32bit-doubles \
// RUN:   -c %s 2>&1 | FileCheck --check-prefix=DFPU-D32 %s
// DFPU-D32: error: invalid argument '-mdfpu' not allowed with '-m32bit-doubles'
// RUN: not %clang --target=rx-none-elf -mcpu=bogus -c %s -o /dev/null 2>&1 \
// RUN:   | FileCheck --check-prefix=BAD-CPU %s
// BAD-CPU: error: unknown target CPU 'bogus'
// BAD-CPU: note: valid target CPU values are: generic, rxv1, rxv2, rxv3, rxv3e, rx100, rx13t, rx140, rx200, rx230, rx600, rx610, rx64m, rx66t, rx71m, rx72t

// The RX options are RX only.
// RUN: not %clang -### --target=x86_64-linux-gnu -m64bit-doubles -c %s 2>&1 \
// RUN:   | FileCheck --check-prefix=OTHER %s
// OTHER: error: unsupported option '-m64bit-doubles' for target 'x86_64-linux-gnu'

// The frame pointer is omitted when optimizing, as GCC does.
// RUN: %clang -### --target=rx-none-elf -O2 -c %s 2>&1 \
// RUN:   | FileCheck --check-prefix=FP-O2 %s
// FP-O2: "-mframe-pointer=none"
// RUN: %clang -### --target=rx-none-elf -O0 -c %s 2>&1 \
// RUN:   | FileCheck --check-prefix=FP-O0 %s
// FP-O0: "-mframe-pointer=all"

// The C library's headers.
// RUN: %clang -### --target=rx-none-elf --gcc-toolchain=%S/Inputs/basic_rx_tree \
// RUN:   -c %s 2>&1 | FileCheck --check-prefix=INC %s
// INC: "-internal-isystem" "{{.*}}basic_rx_tree{{/|\\\\}}lib{{/|\\\\}}gcc{{/|\\\\}}rx-elf{{/|\\\\}}14.2.0{{/|\\\\}}..{{/|\\\\}}..{{/|\\\\}}..{{/|\\\\}}..{{/|\\\\}}rx-elf{{/|\\\\}}include"
// RUN: %clang -### --target=rx-none-elf --gcc-toolchain=%S/Inputs/basic_rx_tree \
// RUN:   -nostdlibinc -c %s 2>&1 | FileCheck --check-prefix=NOINC %s
// NOINC-NOT: "-internal-isystem" "{{.*}}rx-elf{{/|\\\\}}include"

// Linking: rx-elf-gcc's startfile / lib / libgcc / endfile specs, with the
// 64-bit-double multilib by default.
// RUN: %clang -### --target=rx-none-elf --gcc-toolchain=%S/Inputs/basic_rx_tree \
// RUN:   %s 2>&1 | FileCheck --check-prefix=LINK %s
// LINK: "{{.*}}ld.lld" "{{.*}}rx-elf{{/|\\\\}}lib{{/|\\\\}}64-bit-double{{/|\\\\}}crt0.o"
// LINK-SAME: "{{.*}}14.2.0{{/|\\\\}}64-bit-double{{/|\\\\}}crtbegin.o"
// LINK-SAME: "-L{{.*}}14.2.0{{/|\\\\}}64-bit-double"
// LINK-SAME: "-L{{.*}}rx-elf{{/|\\\\}}lib{{/|\\\\}}64-bit-double"
// LINK-SAME: "-L{{.*}}rx-elf{{/|\\\\}}lib"
// LINK-SAME: "-lgcc" "--start-group" "-lc" "-lnosys" "--end-group" "-lgcc"
// LINK-SAME: "-T{{.*}}rx-elf{{/|\\\\}}lib{{/|\\\\}}rx.ld"
// LINK-SAME: "{{.*}}14.2.0{{/|\\\\}}64-bit-double{{/|\\\\}}crtend.o"
// LINK-SAME: "{{.*}}rx-elf{{/|\\\\}}lib{{/|\\\\}}64-bit-double{{/|\\\\}}crtn.o" "-o" "a.out"

// -m32bit-doubles and a CPU without an FPU: the no-fpu-libs multilib;
// -msim: libgloss's simulator library and script.
// RUN: %clang -### --target=rx-none-elf --gcc-toolchain=%S/Inputs/basic_rx_tree \
// RUN:   -m32bit-doubles -mcpu=rx200 -msim %s 2>&1 | FileCheck --check-prefix=LINK-NOFPU %s
// LINK-NOFPU: "{{.*}}ld.lld" "{{.*}}rx-elf{{/|\\\\}}lib{{/|\\\\}}no-fpu-libs{{/|\\\\}}crt0.o"
// LINK-NOFPU-SAME: "{{.*}}14.2.0{{/|\\\\}}no-fpu-libs{{/|\\\\}}crtbegin.o"
// LINK-NOFPU-SAME: "-lgcc" "--start-group" "-lc" "-lsim" "--end-group" "-lgcc"
// LINK-NOFPU-SAME: "-T{{.*}}rx-sim.ld"
// RUN: %clang -### --target=rx-none-elf --gcc-toolchain=%S/Inputs/basic_rx_tree \
// RUN:   -mcpu=rxv3 -mnofpu %s 2>&1 | FileCheck --check-prefix=LINK-NOFPU64 %s
// LINK-NOFPU64: "{{.*}}14.2.0{{/|\\\\}}64-bit-double{{/|\\\\}}no-fpu-libs{{/|\\\\}}crtbegin.o"

// GNU ld, a linker script of one's own, no start files and no C library.
// RUN: %clang -### --target=rx-none-elf --gcc-toolchain=%S/Inputs/basic_rx_tree \
// RUN:   -fuse-ld=bfd -nostartfiles -nolibc -T my.ld %s 2>&1 \
// RUN:   | FileCheck --check-prefix=LINK-BFD %s
// LINK-BFD: "{{.*}}rx-elf{{/|\\\\}}bin{{/|\\\\}}ld.bfd"
// LINK-BFD-NOT: crt0.o
// LINK-BFD-SAME: "-lgcc" "-T" "my.ld" "-o" "a.out"
// RUN: %clang -### --target=rx-none-elf --gcc-toolchain=%S/Inputs/basic_rx_tree \
// RUN:   -nostdlib %s 2>&1 | FileCheck --check-prefix=NOSTDLIB %s
// NOSTDLIB: "{{.*}}ld.lld"
// NOSTDLIB-NOT: "-lgcc"
// NOSTDLIB-NOT: crt0.o

// Without a GCC installation: compiler-rt's builtins.
// RUN: %clang -### --target=rx-none-elf --sysroot= --gcc-toolchain= -nostartfiles \
// RUN:   %s 2>&1 | FileCheck --check-prefix=NOGCC %s
// NOGCC: "{{.*}}ld.lld"
// NOGCC-SAME: libclang_rt.builtins

// GNU as, with -fno-integrated-as: the e_flags and the instruction set
// follow the options.
// RUN: %clang -### --target=rx-none-elf --gcc-toolchain=%S/Inputs/basic_rx_tree \
// RUN:   -fno-integrated-as -mcpu=rxv3 -c %s 2>&1 | FileCheck --check-prefix=AS %s
// AS: "{{.*}}rx-elf{{/|\\\\}}bin{{/|\\\\}}as" "-m64bit-doubles" "-mcpu=rxv3-dfpu"
// RUN: %clang -### --target=rx-none-elf --gcc-toolchain=%S/Inputs/basic_rx_tree \
// RUN:   -fno-integrated-as -m32bit-doubles -mcpu=rx64m -mrelax -c %s 2>&1 \
// RUN:   | FileCheck --check-prefix=AS2 %s
// AS2: "{{.*}}as" "-m32bit-doubles" "-mcpu=rxv2" "-relax"
// RUN: %clang -### --target=rx-none-elf --gcc-toolchain=%S/Inputs/basic_rx_tree \
// RUN:   -fno-integrated-as -misa=v3 -c %s 2>&1 | FileCheck --check-prefix=AS3 %s
// AS3: "{{.*}}as" "-m64bit-doubles" "-mcpu=rxv3"

// The calling convention: CC-RX's unless -mabi=gnu.
// RUN: %clang -### --target=rx-none-elf -c %s 2>&1 \
// RUN:   | FileCheck --check-prefix=ABI-DEF %s
// ABI-DEF-NOT: "-target-abi"
// RUN: %clang -### --target=rx-none-elf -mabi=gnu -c %s 2>&1 \
// RUN:   | FileCheck --check-prefix=ABI-GNU %s
// ABI-GNU: "-cc1" {{.*}}"-target-abi" "gnu"
// RUN: %clang -### --target=rx-none-elf -mabi=ccrx -c %s 2>&1 \
// RUN:   | FileCheck --check-prefix=ABI-CCRX %s
// ABI-CCRX: "-cc1" {{.*}}"-target-abi" "ccrx"

int main(void) { return 0; }
