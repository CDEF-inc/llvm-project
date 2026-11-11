// RUN: %clang_cc1 -triple rx-none-elf -emit-llvm -o - %s | \
// RUN:   FileCheck %s --check-prefix=D64
// RUN: %clang_cc1 -triple rx-none-elf -target-feature -64bit-doubles \
// RUN:   -emit-llvm -o - %s | FileCheck %s --check-prefix=D32

// double and long double are 64 bits by default (GCC's -m64bit-doubles) and
// float with -m32bit-doubles (the 64bit-doubles feature off).

// D64: define{{.*}} double @f(double noundef %d, double noundef %ld)
// D64: attributes #0 = {{.*}}"target-features"="+64bit-doubles"
// D32: define{{.*}} float @f(float noundef %d, float noundef %ld)
// D32: attributes #0 = {{.*}}"target-features"="-64bit-doubles"
double f(double d, long double ld) { return d + ld; }

_Static_assert(sizeof(double) == sizeof(long double), "");
