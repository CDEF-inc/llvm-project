// RUN: %clang_cc1 -triple rx-none-elf -emit-llvm -o - %s | FileCheck %s

// Variadic calls and va_arg as GCC for rx-elf does them: the last named
// argument and the unnamed ones are on the stack, at their natural alignment
// and size (structures byval, so a 3-byte one is followed by the next
// argument at offset 3). va_list points at the next unnamed argument;
// va_arg aligns it to the type's alignment and advances it by the type's
// size (GCC's PARM_BOUNDARY is 8 bits).

#include <stdarg.h>

struct s3 { char c[3]; };
struct s4c { char c[4]; };
struct s8 { int a, b; };
struct s20 { int a[5]; };

int vf(int n, ...);
int vf2(int a, int b, ...);

// Named arguments before the last one still go in registers; structures
// passed through `...` are byval, even those that would otherwise go in
// registers as [N x i32].
// CHECK-LABEL: define{{.*}} void @calls(
// CHECK: call i32 (i32, ...) @vf(i32 noundef 1, ptr noundef byval(%struct.s3) align 1 %{{.*}}, ptr noundef byval(%struct.s4c) align 1 %{{.*}}, ptr noundef byval(%struct.s8) align 4 %{{.*}}, i32 noundef 9)
// CHECK: call i32 (i32, i32, ...) @vf2(i32 noundef 1, i32 noundef 2, i64 noundef 3, double noundef 4.000000e+00, ptr noundef byval(%struct.s20) align 4 %{{.*}})
void calls(struct s3 x, struct s4c y, struct s8 z, struct s20 w) {
  vf(1, x, y, z, 9);
  vf2(1, 2, 3LL, 4.0, w);
}

// The definition takes the named arguments as usual.
// CHECK-LABEL: define{{.*}} i32 @vdef(i32 noundef %n, ...)
int vdef(int n, ...) {
  va_list ap;
  va_start(ap, n);
  int r = 0;

  // A 3-byte structure: no alignment, the pointer advances by 3.
  // CHECK: %argp.cur = load ptr, ptr %ap, align 4
  // CHECK-NEXT: %argp.next = getelementptr inbounds i8, ptr %argp.cur, i32 3
  // CHECK-NEXT: store ptr %argp.next, ptr %ap, align 4
  struct s3 a = va_arg(ap, struct s3);
  r += a.c[0];

  // A char-only 4-byte structure: no alignment either.
  // CHECK: %argp.cur{{[0-9]+}} = load ptr, ptr %ap, align 4
  // CHECK-NEXT: %argp.next{{[0-9]+}} = getelementptr inbounds i8, ptr %argp.cur{{[0-9]+}}, i32 4
  struct s4c b = va_arg(ap, struct s4c);
  r += b.c[3];

  // int: aligned to 4 first.
  // CHECK: %argp.cur{{[0-9]+}} = load ptr, ptr %ap, align 4
  // CHECK-NEXT: [[P:%.*]] = getelementptr inbounds i8, ptr %argp.cur{{[0-9]+}}, i32 3
  // CHECK-NEXT: %argp.cur{{[0-9]+}}.aligned = call ptr @llvm.ptrmask.p0.i32(ptr [[P]], i32 -4)
  // CHECK-NEXT: %argp.next{{[0-9]+}} = getelementptr inbounds i8, ptr %argp.cur{{[0-9]+}}.aligned, i32 4
  r += va_arg(ap, int);

  // long long and double are 4-byte aligned, 8 bytes long.
  // CHECK: call ptr @llvm.ptrmask.p0.i32(ptr {{%.*}}, i32 -4)
  // CHECK-NEXT: %argp.next{{[0-9]+}} = getelementptr inbounds i8, ptr %argp.cur{{[0-9]+}}.aligned, i32 8
  r += (int)va_arg(ap, long long);
  // CHECK: call ptr @llvm.ptrmask.p0.i32(ptr {{%.*}}, i32 -4)
  // CHECK-NEXT: %argp.next{{[0-9]+}} = getelementptr inbounds i8, ptr %argp.cur{{[0-9]+}}.aligned, i32 8
  r += (int)va_arg(ap, double);

  // A 20-byte structure is read in place.
  // CHECK: call ptr @llvm.ptrmask.p0.i32(ptr {{%.*}}, i32 -4)
  // CHECK-NEXT: %argp.next{{[0-9]+}} = getelementptr inbounds i8, ptr %argp.cur{{[0-9]+}}.aligned, i32 20
  struct s20 c = va_arg(ap, struct s20);
  r += c.a[4];

  va_end(ap);
  return r;
}
