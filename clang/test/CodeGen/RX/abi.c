// RUN: %clang_cc1 -triple rx-none-elf -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,CCRX
// RUN: %clang_cc1 -triple rx-none-elf -target-abi ccrx -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,CCRX
// RUN: %clang_cc1 -triple rx-none-elf -target-abi gnu -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,GNU
// RUN: not %clang_cc1 -triple rx-none-elf -target-abi foo -emit-llvm %s \
// RUN:   -o /dev/null 2>&1 | FileCheck %s --check-prefix=BAD

// BAD: error: unknown target ABI 'foo'

// The C ABI of CC-RX (the default; CC-RX manual 9.1) and of GNURX
// (-target-abi gnu; gcc/config/rx/rx.cc): arguments fill R1-R4 by a byte
// count; a structure whose size is a multiple of 4 goes in registers as
// [N x i32] if it fits, any other structure (or one that does not fit) is
// copied to the stack at its natural alignment (byval). CC-RX counts only
// the arguments placed in registers, GNURX all of them. Structures of 1-16
// bytes whose size is a multiple of 4 are returned in R1-R4, the others in
// memory (sret, R15).

struct s1 { char c; };
struct s2 { char c[2]; };
struct s3 { char c[3]; };
struct s4 { int a; };
struct s4c { char c[4]; };
struct sh { short a, b; };
struct s5 { char c[5]; };
struct s6 { short s[3]; };
struct s8 { int a, b; };
struct sd { double d; };
struct sll { long long ll; };
struct sf2 { float a, b; };
struct s12 { int a, b, c; };
struct s16 { int a[4]; };
struct s20 { int a[5]; };
struct empty {};
union u8 { int i; char c[8]; };
struct __attribute__((aligned(8))) sa8 { int a; };

// char, short and _Bool arguments are extended by the caller (CC-RX's
// callee relies on it; the backend extends them again in the callee for
// GNURX's callers), and so are return values.

// CHECK-LABEL: define{{.*}} void @f_char(i8 noundef signext %c, i8 noundef signext %s, i16 noundef signext %h, i16 noundef zeroext %uh, i8 noundef zeroext %b.coerce)
void f_char(char c, signed char s, short h, unsigned short uh, _Bool b) {}
// CHECK-LABEL: define{{.*}} zeroext i8 @r_uchar()
unsigned char r_uchar(void) { return 1; }
// CHECK-LABEL: define{{.*}} signext i8 @r_schar()
signed char r_schar(void) { return 1; }
// CHECK-LABEL: define{{.*}} signext i16 @r_short()
short r_short(void) { return 1; }
// CHECK-LABEL: define{{.*}} zeroext i1 @r_bool()
_Bool r_bool(void) { return 1; }

// CHECK-LABEL: define{{.*}} void @f_wide(i64 noundef %a, double noundef %b, float noundef %c, i32 noundef %d)
void f_wide(long long a, double b, float c, long d) {}
// CHECK-LABEL: define{{.*}} i64 @r_ll()
long long r_ll(void) { return 1; }
// CHECK-LABEL: define{{.*}} double @r_d()
double r_d(void) { return 1; }

// Structures in registers, by size.
// CHECK-LABEL: define{{.*}} void @f_reg(i32 %a.coerce, i32 %b.coerce, i32 %c.coerce, i32 %d.coerce)
void f_reg(struct s4 a, struct s4c b, struct sh c, struct s4 d) {}
// CHECK-LABEL: define{{.*}} void @f_s8([2 x i32] %a.coerce, [2 x i32] %b.coerce)
void f_s8(struct s8 a, struct s8 b) {}
// CHECK-LABEL: define{{.*}} void @f_s16([4 x i32] %a.coerce)
void f_s16(struct s16 a) {}
// CHECK-LABEL: define{{.*}} void @f_s12(i32 noundef %x, [3 x i32] %a.coerce)
void f_s12(int x, struct s12 a) {}
// CHECK-LABEL: define{{.*}} void @f_fp([2 x i32] %a.coerce, [2 x i32] %b.coerce)
void f_fp(struct sd a, struct sll b) {}
// CHECK-LABEL: define{{.*}} void @f_sf2([2 x i32] %a.coerce)
void f_sf2(struct sf2 a) {}
// CHECK-LABEL: define{{.*}} void @f_union([2 x i32] %a.coerce)
void f_union(union u8 a) {}

// Sizes that are not a multiple of 4: on the stack at their own alignment.
// GNURX advances the count by the size rounded up to 4, CC-RX does not.
// CHECK-LABEL: define{{.*}} void @f_odd(ptr noundef byval(%struct.s1) align 1 %a, ptr noundef byval(%struct.s2) align 1 %b, ptr noundef byval(%struct.s3) align 1 %c, ptr noundef byval(%struct.s5) align 1 %d)
void f_odd(struct s1 a, struct s2 b, struct s3 c, struct s5 d) {}
// CHECK-LABEL: define{{.*}} void @f_s6(ptr noundef byval(%struct.s6) align 2 %a, i32 noundef %x)
void f_s6(struct s6 a, int x) {}
// 4 (s3) + 12 = 16: s12 still fits in R2-R4.
// CHECK-LABEL: define{{.*}} void @f_s3_s12(ptr noundef byval(%struct.s3) align 1 %a, [3 x i32] %b.coerce)
void f_s3_s12(struct s3 a, struct s12 b) {}

// Structures that do not fit in what is left of R1-R4. CC-RX puts d in R3,
// which c did not take; for GNURX the count advances past c (8 + 12 = 20),
// so d is on the stack too.
// CCRX-LABEL: define{{.*}} void @f_nofit(i32 noundef %a, i32 noundef %b, ptr noundef byval(%struct.s12) align 4 %c, i32 %d.coerce)
// GNU-LABEL: define{{.*}} void @f_nofit(i32 noundef %a, i32 noundef %b, ptr noundef byval(%struct.s12) align 4 %c, ptr noundef byval(%struct.s4) align 4 %d)
void f_nofit(int a, int b, struct s12 c, struct s4 d) {}
// But a structure that does not go in registers advances GNURX's count only
// by its size rounded up to 4: y is in R2 (in R1 for CC-RX).
// CHECK-LABEL: define{{.*}} void @f_s3_int(ptr noundef byval(%struct.s3) align 1 %a, i32 noundef %y)
void f_s3_int(struct s3 a, int y) {}
// A char-only structure on the stack is packed at byte alignment.
// CHECK-LABEL: define{{.*}} void @f_nofit_c(i64 noundef %a, i64 noundef %b, ptr noundef byval(%struct.s4c) align 1 %c)
void f_nofit_c(long long a, long long b, struct s4c c) {}
// CHECK-LABEL: define{{.*}} void @f_s20(ptr noundef byval(%struct.s20) align 4 %a, i32 noundef %x)
void f_s20(struct s20 a, int x) {}
// The aligned attribute makes it 8 bytes, a multiple of 4.
// CHECK-LABEL: define{{.*}} void @f_sa8(i32 noundef %x, [2 x i32] %a.coerce)
void f_sa8(int x, struct sa8 a) {}
// On the stack it keeps its alignment.
// CHECK-LABEL: define{{.*}} void @f_sa8_stack(i64 noundef %x, i64 noundef %y, ptr noundef byval(%struct.sa8) align 8 %a)
void f_sa8_stack(long long x, long long y, struct sa8 a) {}
// Empty structures (a GNU extension) take no room.
// CHECK-LABEL: define{{.*}} void @f_empty(i32 noundef %x, i32 noundef %y)
void f_empty(int x, struct empty e, int y) {}

// Return values.
// CHECK-LABEL: define{{.*}} i32 @r_s4()
struct s4 r_s4(void) { struct s4 s = {1}; return s; }
// CHECK-LABEL: define{{.*}} i32 @r_s4c()
struct s4c r_s4c(void) { struct s4c s = {{1}}; return s; }
// CHECK-LABEL: define{{.*}} [2 x i32] @r_s8()
struct s8 r_s8(void) { struct s8 s = {1, 2}; return s; }
// CHECK-LABEL: define{{.*}} [2 x i32] @r_sd()
struct sd r_sd(void) { struct sd s = {1}; return s; }
// CHECK-LABEL: define{{.*}} [3 x i32] @r_s12()
struct s12 r_s12(void) { struct s12 s = {1, 2, 3}; return s; }
// CHECK-LABEL: define{{.*}} [4 x i32] @r_s16()
struct s16 r_s16(void) { struct s16 s = {{1}}; return s; }
// CHECK-LABEL: define{{.*}} void @r_s1(ptr {{.*}}sret(%struct.s1) align 1 %agg.result)
struct s1 r_s1(void) { struct s1 s = {1}; return s; }
// CHECK-LABEL: define{{.*}} void @r_s3(ptr {{.*}}sret(%struct.s3) align 1 %agg.result)
struct s3 r_s3(void) { struct s3 s = {{1}}; return s; }
// CHECK-LABEL: define{{.*}} void @r_s6(ptr {{.*}}sret(%struct.s6) align 2 %agg.result)
struct s6 r_s6(void) { struct s6 s = {{1}}; return s; }
// CHECK-LABEL: define{{.*}} void @r_s20(ptr {{.*}}sret(%struct.s20) align 4 %agg.result)
struct s20 r_s20(void) { struct s20 s = {{1}}; return s; }
// GCC returns an empty structure in memory too.
// CHECK-LABEL: define{{.*}} void @r_empty(ptr {{.*}}sret(%struct.empty) align 1 %agg.result)
struct empty r_empty(void) { struct empty e; return e; }
// The sret pointer goes in R15 and does not take an argument register.
// CHECK-LABEL: define{{.*}} void @r_s20_args(ptr {{.*}}sret(%struct.s20) align 4 %agg.result, [4 x i32] %a.coerce)
struct s20 r_s20_args(struct s16 a) { struct s20 s = {{1}}; return s; }

// _Complex is not an aggregate for GCC: in registers in its own mode,
// whatever its size; on the stack at its own alignment.
// CHECK-LABEL: define{{.*}} i16 @r_cchar()
_Complex char r_cchar(void) { return 1; }
// CHECK-LABEL: define{{.*}} [2 x i32] @r_cfloat()
_Complex float r_cfloat(void) { return 1; }
// CHECK-LABEL: define{{.*}} [4 x i32] @r_cdouble()
_Complex double r_cdouble(void) { return 1; }
// c does not fit after 8 bytes; d then takes R3 for CC-RX (as CC-RX puts
// an int after a _Complex double that does not fit), the stack for GNURX.
// CCRX-LABEL: define{{.*}} void @f_cplx(i16 noundef %a.coerce, [2 x i32] noundef %b.coerce, ptr noundef byval({ double, double }) align 4 %c, i16 noundef %d.coerce)
// GNU-LABEL: define{{.*}} void @f_cplx(i16 noundef %a.coerce, [2 x i32] noundef %b.coerce, ptr noundef byval({ double, double }) align 4 %c, ptr noundef byval({ i8, i8 }) align 1 %d)
void f_cplx(_Complex char a, _Complex float b, _Complex double c,
            _Complex char d) {}
