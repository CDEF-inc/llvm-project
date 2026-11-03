// RUN: %clang_cc1 -triple rx-none-elf -emit-llvm %s -o - | FileCheck %s

// C symbols on RX carry a leading underscore: the data layout's `m:o`
// mangling adds it, and the user label prefix agrees with it, so that an asm
// label (which LLDB's expression parser uses for the functions it calls) is
// emitted unmangled instead of tripping clang's assertion.

// CHECK: target datalayout = "e-m:o-p:32:32-i64:32-f64:32-n8:16:32-S32"

// CHECK: define{{.*}} i32 @f(
int f(int x) { return x + 1; }

// CHECK: declare{{.*}} i32 @"\01h"(
int g(int) __asm__("h");

int call(void) { return g(2) + f(1); }
