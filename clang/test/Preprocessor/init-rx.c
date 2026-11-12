// RUN: %clang_cc1 -E -dM -triple rx-none-elf /dev/null | \
// RUN:   FileCheck %s --check-prefixes=CHECK,CCRX,D64
// RUN: %clang -E -dM --target=rx-none-elf -x c /dev/null | \
// RUN:   FileCheck %s --check-prefixes=CHECK,CCRX,D64,RX600,ISA1,FPU,UCHAR
// RUN: %clang -E -dM --target=rx-none-elf -m64bit-doubles -mcpu=rx610 \
// RUN:   -x c /dev/null | FileCheck %s --check-prefixes=CHECK,CCRX,D64,RX610,ISA1,FPU
// RUN: %clang -E -dM --target=rx-none-elf -mcpu=rx200 -x c /dev/null | \
// RUN:   FileCheck %s --check-prefixes=CHECK,CCRX,D64,RX200,ISA1
// RUN: %clang -E -dM --target=rx-none-elf -mcpu=rx100 -x c /dev/null | \
// RUN:   FileCheck %s --check-prefixes=CHECK,CCRX,D64,RX100,ISA1
// RUN: %clang -E -dM --target=rx-none-elf -mnofpu -m32bit-doubles \
// RUN:   -x c /dev/null | FileCheck %s --check-prefixes=CHECK,CCRX,D32,RX600,ISA1
// RUN: %clang -E -dM --target=rx-none-elf -mcpu=rxv1 -x c /dev/null | \
// RUN:   FileCheck %s --check-prefixes=CHECK,CCRX,D64,ISA1
// RUN: %clang -E -dM --target=rx-none-elf -mcpu=rxv2 -x c /dev/null | \
// RUN:   FileCheck %s --check-prefixes=CHECK,CCRX,D64,ISA2,FPU
// RUN: %clang -E -dM --target=rx-none-elf -mcpu=rxv3 -x c /dev/null | \
// RUN:   FileCheck %s --check-prefixes=CHECK,CCRX,D64,ISA3,FPU,DFPU
// RUN: %clang -E -dM --target=rx-none-elf -misa=v3 -mdfpu -x c /dev/null | \
// RUN:   FileCheck %s --check-prefixes=CHECK,CCRX,D64,ISA3,FPU,DFPU
// RUN: %clang -E -dM --target=rx-none-elf -misa=v1 -x c /dev/null | \
// RUN:   FileCheck %s --check-prefixes=CHECK,CCRX,D64,ISA1,FPU
// RUN: %clang -E -dM --target=rx-none-elf -mcpu=rx64m -x c /dev/null | \
// RUN:   FileCheck %s --check-prefixes=CHECK,CCRX,D64,RX64M,ISA2,FPU
// RUN: %clang -E -dM --target=rx-none-elf -mcpu=rx66t -x c /dev/null | \
// RUN:   FileCheck %s --check-prefixes=CHECK,CCRX,D64,RX66T,ISA3,FPU
// RUN: %clang -E -dM --target=rx-none-elf -mcpu=rxv3e -mno-dfpu -x c \
// RUN:   /dev/null | FileCheck %s --check-prefixes=CHECK,CCRX,D64,ISA3,FPU
// RUN: %clang -E -dM --target=rx-none-elf -mabi=ccrx -x c /dev/null | \
// RUN:   FileCheck %s --check-prefixes=CHECK,CCRX,D64,RX600,ISA1,FPU,UCHAR
// RUN: %clang -E -dM --target=rx-none-elf -mabi=gnu -x c /dev/null | \
// RUN:   FileCheck %s --check-prefixes=CHECK,GNU,D64,RX600,ISA1,FPU,UCHAR
// RUN: not %clang -E --target=rx-none-elf -mabi=foo -x c /dev/null 2>&1 | \
// RUN:   FileCheck %s --check-prefix=BAD
// BAD: error: unsupported argument 'foo' to option '-mabi='
// Each ABI defines only its own macro, and neither GCC's __RX_GCC_ABI__ (its
// -mgcc-abi, whose stacked arguments are not naturally aligned).
// RUN: %clang -E -dM --target=rx-none-elf -x c /dev/null | \
// RUN:   FileCheck %s --check-prefix=NOT-CCRX
// RUN: %clang -E -dM --target=rx-none-elf -mabi=gnu -x c /dev/null | \
// RUN:   FileCheck %s --check-prefix=NOT-GNU
// NOT-CCRX-NOT: __RX_GNU_ABI__
// NOT-CCRX-NOT: __RX_GCC_ABI__
// NOT-GNU-NOT: __RX_CCRX_ABI__
// NOT-GNU-NOT: __RX_GCC_ABI__

// The types and macros of rx-elf-gcc 14.2 (TARGET_CPU_CPP_BUILTINS in
// gcc/config/rx/rx.h), by default with -m64bit-doubles, the ABI of the RX
// backend; LLDB's type system uses them too. RXv2, RXv3 and the DFPU as
// GNURX 14.2 defines them: __RXv1__ / __RXv2__ / __RXv3__ (RXv3e too),
// __RX_DFPU_INSNS__, its CPU names, and -misa= with the FPU but without the
// DFPU unless -mdfpu.

// CHECK-DAG: #define __RX__ 1
// CHECK-DAG: #define __RX_LITTLE_ENDIAN__ 1
// CHECK-DAG: #define __RX_GAS_SYNTAX__ 1
// CHECK-DAG: #define __RX_ABI__ 1
// CCRX-DAG: #define __RX_CCRX_ABI__ 1
// GNU-DAG: #define __RX_GNU_ABI__ 1
// CHECK-DAG: #define __RX_ALLOW_STRING_INSNS__ 1
// CHECK-DAG: #define __USER_LABEL_PREFIX__ _
// CHECK-DAG: #define __SIZEOF_POINTER__ 4
// CHECK-DAG: #define __SIZEOF_INT__ 4
// CHECK-DAG: #define __SIZEOF_LONG__ 4
// CHECK-DAG: #define __SIZEOF_LONG_LONG__ 8
// CCRX-DAG: #define __SIZEOF_WCHAR_T__ 2
// GNU-DAG: #define __SIZEOF_WCHAR_T__ 4
// CHECK-DAG: #define __BIGGEST_ALIGNMENT__ 4
// CHECK-DAG: #define __SIZE_TYPE__ long unsigned int
// CHECK-DAG: #define __PTRDIFF_TYPE__ long int
// CHECK-DAG: #define __INTPTR_TYPE__ long int
// CCRX-DAG: #define __WCHAR_TYPE__ unsigned short
// GNU-DAG: #define __WCHAR_TYPE__ long int
// CHECK-DAG: #define __WINT_TYPE__ unsigned int
// CHECK-DAG: #define __CHAR16_TYPE__ unsigned short
// CHECK-DAG: #define __CHAR32_TYPE__ long unsigned int
// CHECK-DAG: #define __INT8_TYPE__ signed char
// CHECK-DAG: #define __INT16_TYPE__ short
// CHECK-DAG: #define __INT32_TYPE__ long int
// CHECK-DAG: #define __INT32_MAX__ 2147483647L
// CHECK-DAG: #define __UINT32_TYPE__ long unsigned int
// CHECK-DAG: #define __UINT32_MAX__ 4294967295UL
// CHECK-DAG: #define __INT64_TYPE__ long long int
// CHECK-DAG: #define __INT_LEAST16_TYPE__ short
// CHECK-DAG: #define __INT_LEAST32_TYPE__ long int
// CHECK-DAG: #define __UINT_LEAST32_TYPE__ long unsigned int
// CHECK-DAG: #define __INT_FAST8_TYPE__ int
// CHECK-DAG: #define __INT_FAST16_TYPE__ int
// CHECK-DAG: #define __INT_FAST32_TYPE__ int
// CHECK-DAG: #define __UINT_FAST8_TYPE__ unsigned int
// CHECK-DAG: #define __UINT_FAST16_TYPE__ unsigned int
// CHECK-DAG: #define __INT_FAST64_TYPE__ long long int
// CHECK-DAG: #define __INTMAX_TYPE__ long long int

// D64-DAG: #define __RX_64BIT_DOUBLES__ 1
// D64-DAG: #define __SIZEOF_DOUBLE__ 8
// D64-DAG: #define __SIZEOF_LONG_DOUBLE__ 8
// D32-DAG: #define __RX_32BIT_DOUBLES__ 1
// D32-DAG: #define __SIZEOF_DOUBLE__ 4
// D32-DAG: #define __SIZEOF_LONG_DOUBLE__ 4
// D32-DAG: #define __DBL_MANT_DIG__ 24

// RX600-DAG: #define __RX600__ 1
// RX610-DAG: #define __RX610__ 1
// RX200-DAG: #define __RX200__ 1
// RX100-DAG: #define __RX100__ 1
// RX64M-DAG: #define __RX64M__ 1
// RX66T-DAG: #define __RX66T__ 1
// ISA1-DAG: #define __RXv1__ 1
// ISA2-DAG: #define __RXv2__ 1
// ISA3-DAG: #define __RXv3__ 1
// FPU-DAG: #define __RX_FPU_INSNS__ 1
// DFPU-DAG: #define __RX_DFPU_INSNS__ 1
// UCHAR-DAG: #define __CHAR_UNSIGNED__ 1

// RUN: %clang -E -dM --target=rx-none-elf -mcpu=rx200 -x c /dev/null | \
// RUN:   FileCheck %s --check-prefix=NOFPU-ONLY
// RUN: %clang -E -dM --target=rx-none-elf -mcpu=rxv2 -x c /dev/null | \
// RUN:   FileCheck %s --check-prefix=NODFPU-ONLY
// NOFPU-ONLY-NOT: __RX_FPU_INSNS__
// NOFPU-ONLY-NOT: __RX_DFPU_INSNS__
// NODFPU-ONLY-NOT: __RX_DFPU_INSNS__
// NODFPU-ONLY-NOT: __RX_GCC_ABI__
// RUN: %clang -E -dM --target=rx-none-elf -misa=v3 -x c /dev/null | \
// RUN:   FileCheck %s --check-prefix=NODFPU-ONLY
// RUN: %clang -E -dM --target=rx-none-elf -mcpu=rx66t -x c /dev/null | \
// RUN:   FileCheck %s --check-prefix=NODFPU-ONLY
// RUN: %clang -E -dM --target=rx-none-elf -misa=v2 -x c /dev/null | \
// RUN:   FileCheck %s --check-prefix=NONAME
// NONAME-NOT: #define __RX{{[0-9][0-9A-Z]*}}__
