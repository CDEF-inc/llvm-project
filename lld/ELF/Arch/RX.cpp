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
//  - R_RX_RH_RELAX (GNU as -relax, or LLVM's -mattr=+relax) marks code the
//    linker may shorten. lld relaxes bsr.a / bra.a (to bsr.w, bra.w / bra.b)
//    and the immediate of mov.l #sym (to #simm24 / 16 / 8), and deletes the
//    alignment padding that is not needed, adjusting symbols and relocations
//    as for RISC-V (relaxOnce / finalizeRelax). Label differences across
//    such code are relocations, so they follow.
//
// The e_flags are merged as GNU ld does by default (its RX emulation passes
// --no-flag-mismatch-warnings): the union of the ABI, 64-bit doubles, DSP
// and PID bits, the string-instruction bits of the first input that sets
// them, and the highest instruction set (RXv2 / RXv3) of the inputs.
//
// The calling convention: LLVM records in each object (an ELF note in
// .note.rx.abi, RXAsmPrinter.cpp) whether it depends on CC-RX's convention,
// on GNURX's, or on neither. Linking objects that depend on different ones
// is an error (-z rx-abi-check=warning makes it a warning, =none skips the
// check). So is linking one that depends on CC-RX's with an object of an
// older LLVM, which has no note and followed GNURX's convention; such an
// object is told by its .comment (clang or rustc). Objects without the note
// otherwise (GCC's, CC-RX's, assembly) are not checked.
//
//===----------------------------------------------------------------------===//

#include "InputFiles.h"
#include "OutputSections.h"
#include "Symbols.h"
#include "SyntheticSections.h"
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

// The ABI note (RXAsmPrinter.cpp): owner "RX", type NT_RX_ABI; descriptor
// the convention the object depends on (below) and its revision.
static constexpr uint32_t NT_RX_ABI = 1;
enum RxAbi : uint32_t { ABI_CCRX = 1, ABI_GNU = 2, ABI_NEUTRAL = 3 };

namespace {
class RX final : public TargetInfo {
public:
  RX(Ctx &);
  uint32_t calcEFlags() const override;
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;
  bool relaxOnce(int pass) const override;
  void finalizeRelax(int passes) const override;

private:
  void checkAbi() const;
};
} // namespace

// R_RX_RH_RELAX addends (include/elf/rx.h).
static constexpr uint32_t RX_RELAXA_IMM6 = 0x10;   // li field at bit 6
static constexpr uint32_t RX_RELAXA_IMM12 = 0x20;  // li field at bit 12
static constexpr uint32_t RX_RELAXA_BRA = 0x200;   // a branch
static constexpr uint32_t RX_RELAXA_ALIGN = 0x10000000;
static constexpr uint32_t RX_RELAXA_ANUM = 0x00ffffff;

RX::RX(Ctx &ctx) : TargetInfo(ctx) {
  // brk
  trapInstr = {0x00, 0x00, 0x00, 0x00};
  // The sections of the GNU tools (P for code, ...) are named by linker
  // scripts; the default layout is the generic one.
  defaultMaxPageSize = 4;
  defaultCommonPageSize = 4;
  defaultImageBase = 0;
}

// What an input says about the calling convention: from its ABI notes (an
// object of ld -r may have several), else from its .comment.
namespace {
struct AbiInfo {
  bool ccrx = false, gnu = false; // it depends on that convention
  bool old = false;               // an older LLVM's (GNURX's convention)
};
} // namespace

static AbiInfo readAbi(ObjFile<ELF32LE> *f) {
  AbiInfo info;
  const ELFFile<ELF32LE> &obj = f->getObj();
  ArrayRef<ELF32LE::Shdr> sections = f->template getELFShdrs<ELF32LE>();
  Expected<StringRef> shstrtab = obj.getSectionStringTable(sections);
  if (!shstrtab) {
    consumeError(shstrtab.takeError());
    return info;
  }
  bool noted = false, llvm = false;
  for (const ELF32LE::Shdr &sec : sections) {
    Expected<StringRef> name = obj.getSectionName(sec, *shstrtab);
    if (!name) {
      consumeError(name.takeError());
      continue;
    }
    bool comment = *name == ".comment";
    if (!comment && *name != ".note.rx.abi")
      continue;
    Expected<ArrayRef<uint8_t>> data = obj.getSectionContents(sec);
    if (!data) {
      consumeError(data.takeError());
      continue;
    }
    if (comment) {
      StringRef s = toStringRef(*data);
      llvm |= s.contains("clang version") || s.contains("rustc version");
      continue;
    }
    // Notes: namesz, descsz, type, name and descriptor padded to 4.
    for (ArrayRef<uint8_t> d = *data; d.size() >= 12;) {
      uint32_t namesz = read32le(d.data()), descsz = read32le(d.data() + 4);
      uint32_t type = read32le(d.data() + 8);
      size_t size = 12 + alignTo(namesz, 4) + alignTo(descsz, 4);
      if (size > d.size())
        break;
      StringRef owner = toStringRef(d.slice(12, namesz)).rtrim('\0');
      if (owner == "RX" && type == NT_RX_ABI && descsz >= 4) {
        noted = true;
        uint32_t abi = read32le(d.data() + 12 + alignTo(namesz, 4));
        info.ccrx |= abi == ABI_CCRX;
        info.gnu |= abi == ABI_GNU;
      }
      d = d.drop_front(size);
    }
  }
  info.old = !noted && llvm;
  return info;
}

void RX::checkAbi() const {
  if (ctx.arg.zRxAbiCheck == 0)
    return;
  InputFile *ccrx = nullptr, *gnu = nullptr, *old = nullptr;
  for (InputFile *f : ctx.objectFiles) {
    AbiInfo info = readAbi(cast<ObjFile<ELF32LE>>(f));
    if (info.ccrx && !ccrx)
      ccrx = f;
    if (info.gnu && !gnu)
      gnu = f;
    if (info.old && !old)
      old = f;
  }
  auto report = [&](auto &&fn) {
    if (ctx.arg.zRxAbiCheck == 2) {
      auto diag = Err(ctx);
      fn(diag);
    } else {
      auto diag = Warn(ctx);
      fn(diag);
    }
  };
  if (ccrx && gnu)
    report([&](auto &diag) {
      diag << ccrx << " uses CC-RX's calling convention (-mabi=ccrx) and "
           << gnu << " GNURX's (-mabi=gnu): calls between them may pass "
           << "arguments in different places; build them with the same "
              "convention (-z rx-abi-check=warning to link anyway)";
    });
  if (ccrx && old)
    report([&](auto &diag) {
      diag << old << " was built by an older LLVM for RX (no ABI note, "
           << "toolchain-2026-11-12 or older), which follows GNURX's calling "
              "convention, and "
           << ccrx << " uses CC-RX's: calls between them may pass arguments "
           << "in different places; rebuild " << old
           << " with this toolchain (-z rx-abi-check=warning to link anyway)";
    });
}

// GNU ld's RX emulation does not warn about mismatched flags by default
// (--no-flag-mismatch-warnings): the output gets the union of the known
// bits of the inputs (so 64-bit doubles if any input has them; a start-up
// file assembled without -m64bit-doubles is common) and the highest
// instruction set. lld does the same.
uint32_t RX::calcEFlags() const {
  checkAbi();
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
    return R_NONE;
  case R_RX_RH_RELAX:
    return R_RELAX_HINT;
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
  // The signed fields take the 32-bit address as a signed number (0xffff8000
  // is -32768), as relaxation picks their sizes (mov.l #sym).
  case R_RX_DIR24S:
    range(int32_t(val), -0x800000, 0x7fffff);
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
    range(int32_t(val), -32768, 65535);
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
    range(int32_t(val), -128, 255);
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

// --- Linker relaxation
//
// For each relocation the pass records the bytes deleted up to it
// (relocDeltas), the relocation's new type (relocTypes) and the bytes that
// replace the instruction's opcode (writes). An R_RX_RH_RELAX with BRA or IMM
// rewrites the bytes before the instruction's field; the field's relocation
// carries the deletion, of the field's top bytes.

// The li field of an immediate, 2 bits at BIT from the start of the
// instruction (MSB first): 1 = simm8, 2 = simm16, 3 = simm24, 0 = imm32.
static unsigned immBytesForLi(unsigned li) { return li ? li : 4; }

static bool relaxSection(Ctx &ctx, int pass, InputSection &sec) {
  const uint64_t secAddr = sec.getVA();
  const MutableArrayRef<Relocation> relocs = sec.relocs();
  auto &aux = *sec.relaxAux;
  ArrayRef<uint8_t> content = sec.content();
  ArrayRef<SymbolAnchor> sa = ArrayRef(aux.anchors);
  bool changed = false;
  uint64_t delta = 0;
  // The deletion and new type that an R_RX_RH_RELAX decided for the next
  // relocation (the instruction's field).
  uint32_t pendingRemove = 0;
  RelType pendingType = R_RX_NONE;

  std::fill_n(aux.relocTypes.get(), relocs.size(), R_RX_NONE);
  aux.writes.clear();
  for (auto [i, r] : llvm::enumerate(relocs)) {
    const uint64_t loc = secAddr + r.offset - delta;
    uint32_t &cur = aux.relocDeltas[i], remove = 0;
    if (pendingType != R_RX_NONE) {
      aux.relocTypes[i] = pendingType;
      remove = pendingRemove;
      pendingType = R_RX_NONE;
    } else if (r.type == R_RX_RH_RELAX &&
               (ctx.arg.relax || (uint32_t(r.addend) & RX_RELAXA_ALIGN))) {
      // With --no-relax only the alignment hints count: the assembler left
      // the most padding, which still has to fit the alignment.
      const uint32_t flags = r.addend;
      const bool hasField = i + 1 < relocs.size();
      // The bytes the previous pass deleted from this instruction; from pass 4
      // on it may not grow, so that the passes converge.
      const uint32_t prev = hasField ? aux.relocDeltas[i + 1] - cur : 0;
      auto allowed = [&](uint32_t want) {
        return pass < 4 || want <= prev;
      };
      if (flags & RX_RELAXA_ALIGN) {
        // LLVM's hint: the alignment - 1 NOPs of padding start here; keep
        // the ones the new address needs.
        const uint64_t align = flags & RX_RELAXA_ANUM;
        const uint64_t need = (align - loc % align) % align;
        if (align == 0 || r.offset + align - 1 > content.size() || need > align - 1) {
          Err(ctx) << getErrorLoc(ctx, content.data() + r.offset)
                   << "bad R_RX_RH_RELAX alignment hint " << flags;
        } else {
          remove = align - 1 - need;
          aux.relocTypes[i] = R_RX_RH_RELAX;
        }
      } else if ((flags & RX_RELAXA_BRA) && hasField &&
                 relocs[i + 1].type == R_RX_DIR24S_PCREL &&
                 relocs[i + 1].offset == r.offset + 1) {
        // bsr.a / bra.a: the displacement is from the instruction.
        const Relocation &f = relocs[i + 1];
        const uint8_t op = content[r.offset];
        const int64_t disp = int64_t(f.sym->getVA(ctx, f.addend) - loc);
        uint8_t newOp = 0;
        RelType newType = R_RX_NONE;
        uint32_t rem = 0;
        if (op == 0x04 && isInt<8>(disp) && allowed(2)) { // bra.b
          newOp = 0x2e, newType = R_RX_DIR8S_PCREL, rem = 2;
        } else if ((op == 0x04 || op == 0x05) && isInt<16>(disp) &&
                   allowed(1)) { // bra.w / bsr.w
          newOp = op == 0x04 ? 0x38 : 0x39, newType = R_RX_DIR16S_PCREL;
          rem = 1;
        }
        if (rem) {
          aux.relocTypes[i] = R_RX_RH_RELAX;
          aux.writes.push_back(newOp);
          pendingType = newType;
          pendingRemove = rem;
        }
      } else if ((flags & (RX_RELAXA_IMM6 | RX_RELAXA_IMM12)) && hasField &&
                 relocs[i + 1].type == R_RX_DIR32 &&
                 relocs[i + 1].offset > r.offset &&
                 relocs[i + 1].offset - r.offset <= 4) {
        // An immediate with an li field: the smallest size the value takes.
        const Relocation &f = relocs[i + 1];
        const unsigned bit = (flags & RX_RELAXA_IMM12) ? 12 : 6;
        const unsigned prefix = f.offset - r.offset;
        const int64_t v =
            int64_t(int32_t(uint32_t(f.sym->getVA(ctx, f.addend))));
        const uint8_t byte = content[r.offset + bit / 8];
        const unsigned shift = 6 - bit % 8;
        if (((byte >> shift) & 3) == 0) { // still imm32
          unsigned li = isInt<8>(v) ? 1 : isInt<16>(v) ? 2 : isInt<24>(v) ? 3 : 0;
          while (li && !allowed(4 - immBytesForLi(li)))
            li = li == 3 ? 0 : li + 1;
          if (li && bit / 8 < prefix) {
            aux.relocTypes[i] = R_RX_RH_RELAX;
            for (unsigned k = 0; k != prefix; ++k) {
              uint8_t b = content[r.offset + k];
              if (k == bit / 8)
                b = (b & ~(3 << shift)) | (li << shift);
              aux.writes.push_back(b);
            }
            pendingType = li == 1 ? R_RX_DIR8S : li == 2 ? R_RX_DIR16S : R_RX_DIR24S;
            pendingRemove = 4 - immBytesForLi(li);
          }
        }
      }
    }

    // Anchors at or before this relocation follow the deletions before it.
    for (; sa.size() && sa[0].offset <= r.offset; sa = sa.slice(1)) {
      if (sa[0].end)
        sa[0].d->size = sa[0].offset - delta - sa[0].d->value;
      else
        sa[0].d->value = sa[0].offset - delta;
    }
    delta += remove;
    if (delta != cur) {
      cur = delta;
      changed = true;
    }
  }
  for (const SymbolAnchor &a : sa) {
    if (a.end)
      a.d->size = a.offset - delta - a.d->value;
    else
      a.d->value = a.offset - delta;
  }
  sec.bytesDropped = delta;
  return changed;
}

// Whether SEC can be relaxed: it has R_RX_RH_RELAX and its relocations are in
// offset order (the deltas assume it).
static bool hasRelaxHints(const InputSection &sec) {
  ArrayRef<Relocation> rels = sec.relocs();
  bool hint = false;
  for (size_t i = 0; i != rels.size(); ++i) {
    hint |= rels[i].type == R_RX_RH_RELAX;
    if (i && rels[i].offset < rels[i - 1].offset)
      return false;
  }
  return hint;
}

bool RX::relaxOnce(int pass) const {
  if (pass == 0)
    initSymbolAnchors(ctx);
  SmallVector<InputSection *, 0> storage;
  bool changed = false;
  for (OutputSection *osec : ctx.outputSections) {
    if (!(osec->flags & SHF_EXECINSTR))
      continue;
    for (InputSection *sec : getInputSections(*osec, storage))
      if (sec->relaxAux && sec->relaxAux->relocDeltas && hasRelaxHints(*sec))
        changed |= relaxSection(ctx, pass, *sec);
  }
  return changed;
}

void RX::finalizeRelax(int passes) const {
  SmallVector<InputSection *, 0> storage;
  for (OutputSection *osec : ctx.outputSections) {
    if (!(osec->flags & SHF_EXECINSTR))
      continue;
    for (InputSection *sec : getInputSections(*osec, storage)) {
      if (!sec->relaxAux || !sec->relaxAux->relocDeltas || !hasRelaxHints(*sec))
        continue;
      RelaxAux &aux = *sec->relaxAux;
      MutableArrayRef<Relocation> rels = sec->relocs();
      ArrayRef<uint8_t> old = sec->content();
      const size_t newSize = old.size() - aux.relocDeltas[rels.size() - 1];
      uint8_t *p = ctx.bAlloc.Allocate<uint8_t>(newSize);
      size_t writesIdx = 0;
      uint64_t offset = 0;
      uint32_t delta = 0;
      sec->content_ = p;
      sec->size = newSize;
      sec->bytesDropped = 0;

      for (size_t i = 0, e = rels.size(); i != e; ++i) {
        const uint32_t remove = aux.relocDeltas[i] - delta;
        delta = aux.relocDeltas[i];
        if (remove == 0 && aux.relocTypes[i] == R_RX_NONE)
          continue;
        const Relocation &r = rels[i];
        memcpy(p, old.data() + offset, r.offset - offset);
        p += r.offset - offset;
        uint64_t skip = 0;
        if (r.type == R_RX_RH_RELAX && (r.addend & RX_RELAXA_ALIGN)) {
          // Keep the NOPs the alignment needs.
          skip = (r.addend & RX_RELAXA_ANUM) - 1 - remove;
          memset(p, 0x03, skip);
        } else if (r.type == R_RX_RH_RELAX) {
          // The new opcode bytes, up to the field.
          skip = i + 1 < e ? rels[i + 1].offset - r.offset : 0;
          for (uint64_t k = 0; k != skip; ++k)
            p[k] = aux.writes[writesIdx++];
        } else {
          // The field: its low bytes stay, the top ones go.
          skip = r.type == R_RX_DIR8S || r.type == R_RX_DIR8S_PCREL ? 1
                 : r.type == R_RX_DIR16S || r.type == R_RX_DIR16S_PCREL ? 2
                 : 3;
          if (aux.relocTypes[i] != R_RX_NONE)
            skip = aux.relocTypes[i] == R_RX_DIR8S ||
                           aux.relocTypes[i] == R_RX_DIR8S_PCREL
                       ? 1
                   : aux.relocTypes[i] == R_RX_DIR16S ||
                           aux.relocTypes[i] == R_RX_DIR16S_PCREL
                       ? 2
                       : 3;
          memcpy(p, old.data() + r.offset, skip);
        }
        p += skip;
        offset = r.offset + skip + remove;
      }
      memcpy(p, old.data() + offset, old.size() - offset);

      // Relocations at one offset (an R_RX_SYM / OP / ABS group) move
      // together, by the deletions before them.
      delta = 0;
      for (size_t i = 0, e = rels.size(); i != e;) {
        const uint64_t cur = rels[i].offset;
        do {
          rels[i].offset -= delta;
          if (aux.relocTypes[i] != R_RX_NONE && rels[i].type != R_RX_RH_RELAX)
            rels[i].type = aux.relocTypes[i];
        } while (++i != e && rels[i].offset == cur);
        delta = aux.relocDeltas[i - 1];
      }
    }
  }
}

void elf::setRXTargetInfo(Ctx &ctx) { ctx.target.reset(new RX(ctx)); }
