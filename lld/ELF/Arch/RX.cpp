//===- RX.cpp -------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Renesas RX: a 32-bit little-endian CISC microcontroller with variable-length
// instructions. The relocations follow GNU ld (bfd/elf32-rx.c):
//
//  - R_RX_DIR*: the value is S + A, written little-endian (code and data;
//    big-endian data is not supported). The PC-relative ones measure from the
//    start of the instruction, whose 8 / 16 / 24-bit field is one byte into
//    it, so the value is S + A - P + 1 (the 3-bit field of bra.s / beq.s /
//    bne.s is in the opcode byte itself: S + A - P).
//  - R_RX_SYM / R_RX_OP* / R_RX_ABS*: a stack machine for expressions such as
//    "a - b" that the assembler cannot resolve. R_RX_SYM pushes S + A, the
//    operators pop their operands and push the result, and R_RX_ABS* pops
//    the value and writes it. All of them are at the same offset, in order.
//  - R_RX_RH_RELAX marks code GNU ld may relax (with --relax); lld does not
//    relax and ignores it.
//
// The e_flags are merged as GNU ld does by default (its RX emulation passes
// --no-flag-mismatch-warnings): the union of the ABI, 64-bit doubles, DSP
// and PID bits, the string-instruction bits of the first input that sets
// them, and the highest instruction set (RXv2 / RXv3) of the inputs.
//
//===----------------------------------------------------------------------===//

#include "InputFiles.h"
#include "Symbols.h"
#include "Target.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

// e_flags (binutils include/elf/rx.h).
static constexpr uint32_t EF_RX_64BIT_DOUBLES = 1 << 0;
static constexpr uint32_t EF_RX_DSP = 1 << 1;
static constexpr uint32_t EF_RX_PID = 1 << 2;
static constexpr uint32_t EF_RX_ABI = 1 << 3;
static constexpr uint32_t EF_RX_SINSNS_SET = 1 << 6;
static constexpr uint32_t EF_RX_SINSNS_MASK = 3 << 6;
static constexpr uint32_t EF_RX_V2 = 1 << 8;
static constexpr uint32_t EF_RX_V3 = 1 << 9;

namespace {
class RX final : public TargetInfo {
public:
  RX(Ctx &);
  uint32_t calcEFlags() const override;
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;
};
} // namespace

RX::RX(Ctx &ctx) : TargetInfo(ctx) {
  // brk
  trapInstr = {0x00, 0x00, 0x00, 0x00};
  // The sections of the GNU tools (P for code, ...) are named by linker
  // scripts; the default layout is the generic one.
  defaultMaxPageSize = 4;
  defaultCommonPageSize = 4;
  defaultImageBase = 0;
}

// GNU ld's RX emulation does not warn about mismatched flags by default
// (--no-flag-mismatch-warnings): the output gets the union of the known
// bits of the inputs (so 64-bit doubles if any input has them; a start-up
// file assembled without -m64bit-doubles is common) and the highest
// instruction set. lld does the same.
uint32_t RX::calcEFlags() const {
  const uint32_t known = EF_RX_ABI | EF_RX_64BIT_DOUBLES | EF_RX_DSP |
                         EF_RX_PID | EF_RX_SINSNS_MASK;
  uint32_t out = 0, isa = 0;
  bool sinsnsSet = false;
  for (InputFile *f : ctx.objectFiles) {
    uint32_t flags = cast<ObjFile<ELF32LE>>(f)->getObj().getHeader().e_flags;
    isa |= flags & (EF_RX_V2 | EF_RX_V3);
    // String-instruction use counts only where it is recorded.
    if (flags & EF_RX_SINSNS_SET) {
      if (!sinsnsSet)
        out = (out & ~EF_RX_SINSNS_MASK) | (flags & EF_RX_SINSNS_MASK);
      sinsnsSet = true;
      flags &= ~EF_RX_SINSNS_MASK;
    }
    out |= flags & known & ~EF_RX_SINSNS_MASK;
  }
  if (isa & EF_RX_V3)
    isa = EF_RX_V3;
  return out | isa;
}

RelExpr RX::getRelExpr(RelType type, const Symbol &s,
                       const uint8_t *loc) const {
  switch (type) {
  case R_RX_NONE:
  case R_RX_RH_RELAX:
    return R_NONE;
  case R_RX_DIR3U_PCREL:
  case R_RX_DIR8S_PCREL:
  case R_RX_DIR16S_PCREL:
  case R_RX_DIR24S_PCREL:
    return R_PC;
  default:
    // The direct relocations and R_RX_SYM need S + A; for the operators and
    // R_RX_ABS* the value is ignored.
    return R_ABS;
  }
}

// The expression stack of R_RX_SYM / R_RX_OP* / R_RX_ABS*. A section's
// relocations are applied in order by one thread, and the relocations of
// one expression are consecutive, so a stack per thread suffices.
static thread_local SmallVector<int32_t, 8> rxStack;

static int32_t pop(Ctx &ctx, uint8_t *loc, const Relocation &rel) {
  if (rxStack.empty()) {
    Err(ctx) << getErrorLoc(ctx, loc) << rel.type
             << " with an empty relocation stack";
    return 0;
  }
  return rxStack.pop_back_val();
}

// Writes the low N bytes of V little-endian.
static void writeLE(uint8_t *loc, uint64_t v, unsigned bytes) {
  for (unsigned i = 0; i != bytes; ++i)
    loc[i] = v >> (8 * i);
}

static void writeBE(uint8_t *loc, uint64_t v, unsigned bytes) {
  for (unsigned i = 0; i != bytes; ++i)
    loc[bytes - 1 - i] = v >> (8 * i);
}

void RX::relocate(uint8_t *loc, const Relocation &rel, uint64_t val) const {
  int64_t v = static_cast<int64_t>(val);
  // [lo, hi] as GNU ld checks it (some accept both signed and unsigned).
  auto range = [&](int64_t x, int64_t lo, int64_t hi) {
    if (x < lo || x > hi)
      Err(ctx) << getErrorLoc(ctx, loc) << "relocation " << rel.type
               << " out of range: " << x << " is not in [" << lo << ", "
               << hi << "]";
  };
  auto aligned = [&](int64_t x, unsigned scale) {
    if (x % scale)
      Err(ctx) << getErrorLoc(ctx, loc) << "relocation " << rel.type
               << " is not aligned to " << scale << " bytes: " << x;
  };

  switch (rel.type) {
  case R_RX_NONE:
  case R_RX_RH_RELAX:
    return;

  // --- direct
  case R_RX_DIR32:
    write32le(loc, val);
    return;
  case R_RX_DIR32_REV:
    write32be(loc, val);
    return;
  case R_RX_DIR24S:
    range(v, -0x800000, 0x7fffff);
    writeLE(loc, val, 3);
    return;
  case R_RX_DIR16:
    range(v, -32768, 65535);
    write16le(loc, val);
    return;
  case R_RX_DIR16U:
    range(v, 0, 65535);
    write16le(loc, val);
    return;
  case R_RX_DIR16S:
    range(v, -32768, 65535);
    write16le(loc, val);
    return;
  case R_RX_DIR16_REV:
    range(v, -32768, 65535);
    write16be(loc, val);
    return;
  case R_RX_DIR16UL:
  case R_RX_DIR16UW: {
    unsigned scale = rel.type == R_RX_DIR16UL ? 4 : 2;
    aligned(v, scale);
    range(v / scale, 0, 65535);
    write16le(loc, val / scale);
    return;
  }
  case R_RX_DIR8:
    range(v, -128, 255);
    *loc = val;
    return;
  case R_RX_DIR8U:
    range(v, 0, 255);
    *loc = val;
    return;
  case R_RX_DIR8S:
    range(v, -128, 255);
    *loc = val;
    return;
  case R_RX_DIR8UL:
  case R_RX_DIR8UW: {
    unsigned scale = rel.type == R_RX_DIR8UL ? 4 : 2;
    aligned(v, scale);
    range(v / scale, 0, 255);
    *loc = val / scale;
    return;
  }

  // --- PC-relative (val = S + A - P, P the field's address)
  case R_RX_DIR3U_PCREL:
    range(v, 3, 10);
    *loc = (*loc & 0xf8) | (val & 0x07);
    return;
  case R_RX_DIR8S_PCREL:
    range(v + 1, -128, 127);
    *loc = val + 1;
    return;
  case R_RX_DIR16S_PCREL:
    range(v + 1, -32768, 32767);
    write16le(loc, val + 1);
    return;
  case R_RX_DIR24S_PCREL:
    range(v + 1, -0x800000, 0x7fffff);
    writeLE(loc, val + 1, 3);
    return;

  // --- the expression stack
  case R_RX_SYM:
    rxStack.push_back(static_cast<int32_t>(val));
    return;
  case R_RX_OPneg:
  case R_RX_OPnot: {
    uint32_t a = pop(ctx, loc, rel);
    rxStack.push_back(rel.type == R_RX_OPneg ? -a : ~a);
    return;
  }
  case R_RX_OPadd:
  case R_RX_OPsub:
  case R_RX_OPmul:
  case R_RX_OPdiv:
  case R_RX_OPshla:
  case R_RX_OPshra:
  case R_RX_OPand:
  case R_RX_OPor:
  case R_RX_OPxor:
  case R_RX_OPmod: {
    // GNU ld pops the top (t) and then the next (n); only OPsub computes
    // n op t, the others t op n. The values are 32-bit and wrap around
    // (GNU ld's int32_t arithmetic, without its undefined behaviour), so
    // INT32_MIN / -1 is INT32_MIN and INT32_MIN % -1 is 0.
    int32_t t = pop(ctx, loc, rel), n = pop(ctx, loc, rel), r = 0;
    uint32_t ut = t, un = n;
    switch (rel.type) {
    case R_RX_OPadd: r = ut + un; break;
    case R_RX_OPsub: r = un - ut; break;
    case R_RX_OPmul: r = ut * un; break;
    case R_RX_OPdiv:
    case R_RX_OPmod:
      if (n == 0) {
        Err(ctx) << getErrorLoc(ctx, loc) << rel.type << ": division by zero";
        break;
      }
      if (n == -1)
        r = rel.type == R_RX_OPdiv ? -ut : 0;
      else
        r = rel.type == R_RX_OPdiv ? t / n : t % n;
      break;
    case R_RX_OPshla:
    case R_RX_OPshra:
      if (n < 0 || n > 31) {
        Err(ctx) << getErrorLoc(ctx, loc) << rel.type << ": shift count " << n
                 << " is not in [0, 31]";
        break;
      }
      r = rel.type == R_RX_OPshla ? ut << n : t >> n;
      break;
    case R_RX_OPand: r = t & n; break;
    case R_RX_OPor: r = t | n; break;
    case R_RX_OPxor: r = t ^ n; break;
    default: break;
    }
    rxStack.push_back(r);
    return;
  }
  case R_RX_ABS32:
    write32le(loc, pop(ctx, loc, rel));
    return;
  case R_RX_ABS32_REV:
    write32be(loc, pop(ctx, loc, rel));
    return;
  case R_RX_ABS24S:
  case R_RX_ABS24S_PCREL: {
    int32_t x = pop(ctx, loc, rel);
    range(x, -0x800000, 0x7fffff);
    writeLE(loc, x, 3);
    return;
  }
  case R_RX_ABS16:
  case R_RX_ABS16_REV: {
    int32_t x = pop(ctx, loc, rel);
    range(x, -32768, 65535);
    if (rel.type == R_RX_ABS16)
      write16le(loc, x);
    else
      writeBE(loc, x, 2);
    return;
  }
  case R_RX_ABS16S:
  case R_RX_ABS16S_PCREL: {
    int32_t x = pop(ctx, loc, rel);
    range(x, -32768, 32767);
    write16le(loc, x);
    return;
  }
  case R_RX_ABS16U:
  case R_RX_ABS16UL:
  case R_RX_ABS16UW: {
    int32_t x = pop(ctx, loc, rel);
    x >>= rel.type == R_RX_ABS16UL ? 2 : rel.type == R_RX_ABS16UW ? 1 : 0;
    range(x, 0, 65535);
    write16le(loc, x);
    return;
  }
  case R_RX_ABS8: {
    int32_t x = pop(ctx, loc, rel);
    range(x, -128, 255);
    *loc = x;
    return;
  }
  case R_RX_ABS8U:
  case R_RX_ABS8UL:
  case R_RX_ABS8UW: {
    int32_t x = pop(ctx, loc, rel);
    x >>= rel.type == R_RX_ABS8UL ? 2 : rel.type == R_RX_ABS8UW ? 1 : 0;
    range(x, 0, 255);
    *loc = x;
    return;
  }
  case R_RX_ABS8S:
  case R_RX_ABS8S_PCREL: {
    int32_t x = pop(ctx, loc, rel);
    range(x, -128, 127);
    *loc = x;
    return;
  }

  default:
    // The Red Hat relaxation relocations (R_RX_RH_* other than RELAX),
    // the GP-relative ones and the section-based operators (R_RX_OPsctsize,
    // R_RX_OPscttop, R_RX_OPromtop, R_RX_OPramtop) are not supported.
    Err(ctx) << getErrorLoc(ctx, loc) << "unsupported relocation " << rel.type;
  }
}

void elf::setRXTargetInfo(Ctx &ctx) { ctx.target.reset(new RX(ctx)); }
