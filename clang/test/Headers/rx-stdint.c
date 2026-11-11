// RUN: %clang_cc1 -triple rx-none-elf -ffreestanding -std=c23 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple rx-none-elf -ffreestanding -std=c99 -fsyntax-only -verify %s
// expected-no-diagnostics

// clang's own stdint.h (freestanding, or without newlib) gives RX GCC's and
// newlib's types: int32_t / int_least32_t are long, int_fast8_t to
// int_fast32_t are int.

#include <stdint.h>

#define SAME(T, U) _Static_assert(_Generic((T)0, U: 1, default: 0), #T)

SAME(int32_t, long);
SAME(uint32_t, unsigned long);
SAME(int_least32_t, long);
SAME(int_least16_t, short);
SAME(int_least8_t, signed char);
SAME(int_fast8_t, int);
SAME(uint_fast8_t, unsigned int);
SAME(int_fast16_t, int);
SAME(uint_fast16_t, unsigned int);
SAME(int_fast32_t, int);
SAME(uint_fast32_t, unsigned int);
SAME(int_fast64_t, long long);

_Static_assert(INT_FAST8_MAX == 2147483647, "");
_Static_assert(INT_FAST8_MIN == -2147483647 - 1, "");
_Static_assert(UINT_FAST16_MAX == 4294967295U, "");
_Static_assert(INT_FAST32_MAX == 2147483647, "");
_Static_assert(INT_LEAST16_MAX == 32767, "");
_Static_assert(INT32_MAX == 2147483647L, "");
#if __STDC_VERSION__ >= 202311L
_Static_assert(INT_FAST8_WIDTH == 32, "");
_Static_assert(UINT_FAST16_WIDTH == 32, "");
_Static_assert(INT_LEAST8_WIDTH == 8, "");
#endif
