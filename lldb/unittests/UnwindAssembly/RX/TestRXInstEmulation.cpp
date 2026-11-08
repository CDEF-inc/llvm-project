//===-- TestRXInstEmulation.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "gtest/gtest.h"

#include "Plugins/UnwindAssembly/InstEmulation/UnwindAssemblyInstEmulation.h"

#include "lldb/Core/AddressRange.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Utility/ArchSpec.h"

#include "Plugins/Disassembler/LLVMC/DisassemblerLLVMC.h"
#include "Plugins/Instruction/RX/EmulateInstructionRX.h"
#include "llvm/Support/TargetSelect.h"

using namespace lldb;
using namespace lldb_private;

namespace {
// DWARF register numbers.
enum { r0 = 0, r6 = 6, r7 = 7, r13 = 13, pc = 17 };

class TestRXInstEmulation : public testing::Test {
public:
  static void SetUpTestCase() {
    llvm::InitializeAllTargets();
    llvm::InitializeAllAsmPrinters();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllDisassemblers();
    DisassemblerLLVMC::Initialize();
    EmulateInstructionRX::Initialize();
  }

  static void TearDownTestCase() {
    DisassemblerLLVMC::Terminate();
    EmulateInstructionRX::Terminate();
  }

protected:
  // The unwind plan for the function in `data`, at 0x1000.
  UnwindPlan Analyze(std::vector<uint8_t> data) {
    ArchSpec arch("rx-none-elf");
    std::unique_ptr<UnwindAssemblyInstEmulation> engine(
        static_cast<UnwindAssemblyInstEmulation *>(
            UnwindAssemblyInstEmulation::CreateInstance(arch)));
    EXPECT_NE(nullptr, engine);
    UnwindPlan plan(eRegisterKindDWARF);
    if (!engine)
      return plan;
    AddressRange range(0x1000, data.size());
    EXPECT_TRUE(engine->GetNonCallSiteUnwindPlanFromAssembly(
        range, data.data(), data.size(), plan));
    return plan;
  }

  static void ExpectCFA(const UnwindPlan &plan, int64_t offset, uint32_t reg,
                        int32_t cfa_offset) {
    const UnwindPlan::Row *row = plan.GetRowForFunctionOffset(offset);
    ASSERT_NE(nullptr, row);
    EXPECT_TRUE(row->GetCFAValue().IsRegisterPlusOffset()) << "at " << offset;
    EXPECT_EQ(reg, row->GetCFAValue().GetRegisterNumber()) << "at " << offset;
    EXPECT_EQ(cfa_offset, row->GetCFAValue().GetOffset()) << "at " << offset;
  }

  static void ExpectSaved(const UnwindPlan &plan, int64_t offset, uint32_t reg,
                          int32_t cfa_offset) {
    const UnwindPlan::Row *row = plan.GetRowForFunctionOffset(offset);
    ASSERT_NE(nullptr, row);
    UnwindPlan::Row::AbstractRegisterLocation loc;
    ASSERT_TRUE(row->GetRegisterInfo(reg, loc)) << "r" << reg << " at "
                                                << offset;
    EXPECT_TRUE(loc.IsAtCFAPlusOffset()) << "r" << reg << " at " << offset;
    EXPECT_EQ(cfa_offset, loc.GetOffset()) << "r" << reg << " at " << offset;
  }
};
} // namespace

// The function entry: the call pushed the return address.
TEST_F(TestRXInstEmulation, Entry) {
  UnwindPlan plan = Analyze({0x03, 0x02}); // nop; rts
  ExpectCFA(plan, 0, r0, 4);
  ExpectSaved(plan, 0, pc, -4);
}

// The RX backend's frame: pushm of the callee-saved range, SP down for the
// locals, rtsd.
TEST_F(TestRXInstEmulation, PushmFrame) {
  UnwindPlan plan = Analyze({
      0x6e, 0x6d,       // 0: pushm r6-r13
      0x71, 0x00, 0xf8, // 2: add #-8, r0, r0
      0x03,             // 5: nop
      0x3f, 0x6d, 0x0b, // 6: rtsd #44, r6-r13
  });
  ExpectCFA(plan, 0, r0, 4);
  ExpectCFA(plan, 2, r0, 36);
  ExpectSaved(plan, 2, r13, -8);
  ExpectSaved(plan, 2, r6, -36);
  ExpectCFA(plan, 5, r0, 44);
  ExpectSaved(plan, 5, pc, -4);
}

// GCC's frame: push.l, sub #n, r0.
TEST_F(TestRXInstEmulation, PushFrame) {
  UnwindPlan plan = Analyze({
      0x7e, 0xa7,       // 0: push.l r7
      0x60, 0x40,       // 2: sub #4, r0
      0x03,             // 4: nop
      0x3f, 0x77, 0x02, // 5: rtsd #8, r7-r7
  });
  ExpectCFA(plan, 2, r0, 8);
  ExpectSaved(plan, 2, r7, -8);
  ExpectCFA(plan, 4, r0, 12);
}

// With a frame pointer the CFA follows R6, whatever SP does afterwards.
TEST_F(TestRXInstEmulation, FramePointer) {
  UnwindPlan plan = Analyze({
      0x6e, 0x67,       // 0: pushm r6-r7
      0x71, 0x06, 0xf4, // 2: add #-12, r0, r6
      0xef, 0x60,       // 5: mov.l r6, r0
      0x03,             // 7: nop
      0xef, 0x60,       // 8: mov.l r6, r0
      0x3f, 0x67, 0x05, // 10: rtsd #20, r6-r7
  });
  ExpectCFA(plan, 2, r0, 12);
  ExpectCFA(plan, 5, r6, 24);
  ExpectCFA(plan, 7, r6, 24);
  ExpectSaved(plan, 7, r6, -12);
}

// The block after a mid-function return gets the state of the branch to it.
TEST_F(TestRXInstEmulation, BranchPastReturn) {
  UnwindPlan plan = Analyze({
      0x6e, 0x67,       // 0: pushm r6-r7
      0x20, 0x06,       // 2: beq.b 8
      0x03,             // 4: nop
      0x3f, 0x67, 0x03, // 5: rtsd #12, r6-r7
      0x0b,             // 8: bra.s 11
      0x03, 0x03,       // 9: nop; nop
      0x03,             // 11: nop
      0x3f, 0x67, 0x03, // 12: rtsd #12, r6-r7
  });
  ExpectCFA(plan, 8, r0, 12);
  ExpectSaved(plan, 8, r7, -8);
  ExpectCFA(plan, 11, r0, 12);
}

// popm then rts: the registers come back and the CFA returns to SP + 4.
TEST_F(TestRXInstEmulation, PopmEpilogue) {
  UnwindPlan plan = Analyze({
      0x6e, 0x67,       // 0: pushm r6-r7
      0x71, 0x00, 0xf8, // 2: add #-8, r0, r0
      0x62, 0x80,       // 5: add #8, r0
      0x6f, 0x67,       // 7: popm r6-r7
      0x02,             // 9: rts
  });
  ExpectCFA(plan, 5, r0, 20);
  ExpectCFA(plan, 7, r0, 12);
  ExpectCFA(plan, 9, r0, 4);
  const UnwindPlan::Row *row = plan.GetRowForFunctionOffset(9);
  ASSERT_NE(nullptr, row);
  UnwindPlan::Row::AbstractRegisterLocation loc;
  ASSERT_TRUE(row->GetRegisterInfo(r6, loc));
  EXPECT_TRUE(loc.IsSame());
}
