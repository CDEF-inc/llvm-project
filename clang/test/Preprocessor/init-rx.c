// RUN: %clang_cc1 -E -dM -triple rx-none-elf /dev/null | FileCheck %s

// The types and macros of GCC for rx-elf with -m64bit-doubles (the ABI of
// the RX backend), which LLDB's type system uses.

// CHECK-DAG: #define __RX__ 1
// CHECK-DAG: #define __RX_LITTLE_ENDIAN__ 1
// CHECK-DAG: #define __RX_64BIT_DOUBLES__ 1
// CHECK-DAG: #define __RX_GCC_ABI__ 1
// CHECK-DAG: #define __USER_LABEL_PREFIX__ _
// CHECK-DAG: #define __SIZEOF_POINTER__ 4
// CHECK-DAG: #define __SIZEOF_LONG__ 4
// CHECK-DAG: #define __SIZEOF_LONG_LONG__ 8
// CHECK-DAG: #define __SIZEOF_DOUBLE__ 8
// CHECK-DAG: #define __SIZEOF_LONG_DOUBLE__ 8
// CHECK-DAG: #define __SIZE_TYPE__ long unsigned int
// CHECK-DAG: #define __PTRDIFF_TYPE__ long int
// CHECK-DAG: #define __BIGGEST_ALIGNMENT__ 4
