// RUN: %clang_cc1 -triple rx-none-elf -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple rx-none-elf -fms-layout-compatibility=itanium \
// RUN:   -DITANIUM -fsyntax-only -verify %s
// expected-no-diagnostics

// Record layout as rx-elf-gcc 14.2 lays it out (sizes and alignments
// measured with it). long long and double are 4-byte aligned, also for
// __alignof__. Bit-fields use the MS layout (TARGET_MS_BITFIELD_LAYOUT_P in
// gcc/config/rx/rx.cc): a bit-field of another type starts a new storage
// unit of its type. That unit keeps the type's alignment (4 for long long),
// and the packed attribute turns the MS layout off.

#define CHECK(T, S, A)                                                         \
  _Static_assert(sizeof(T) == S, #T);                                          \
  _Static_assert(_Alignof(T) == A, #T);                                        \
  _Static_assert(__alignof__(T) == A, #T)

CHECK(long long, 8, 4);
CHECK(double, 8, 4);
CHECK(long double, 8, 4);
CHECK(struct { long long ll; }, 8, 4);
CHECK(struct { char c; long long ll; }, 12, 4);
CHECK(struct { char c; double d; }, 12, 4);
CHECK(struct { char c; short s; }, 4, 2);

#ifndef ITANIUM
CHECK(struct { int a : 3; char b : 4; short c : 9; }, 8, 4);
CHECK(struct { char a; int b : 3; }, 8, 4);
CHECK(struct { char a : 4; char b; int c : 20; long long d : 5; }, 16, 4);
CHECK(struct { char c; long long x : 3; }, 12, 4);
CHECK(struct { char a; long long b : 40; char c; }, 16, 4);
CHECK(struct { char a : 3; int : 0; char b; }, 8, 4);
CHECK(struct { int a : 7; unsigned b : 9; char c; }, 8, 4);
#else
// -mno-ms-bitfields: GCC's usual layout.
CHECK(struct { char a; int b : 3; }, 4, 4);
#endif

struct __attribute__((packed)) pk { char a : 2; short b : 3; int c : 4; };
CHECK(struct pk, 2, 1);
