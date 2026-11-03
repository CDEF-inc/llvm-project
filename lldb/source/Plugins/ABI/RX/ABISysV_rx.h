//===-- ABISysV_rx.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_ABI_RX_ABISYSV_RX_H
#define LLDB_SOURCE_PLUGINS_ABI_RX_ABISYSV_RX_H

#include "lldb/Target/ABI.h"
#include "lldb/lldb-private.h"

// The Renesas RX ABI of GCC (rx-elf): arguments in r1-r4, results in r1
// (r1:r2 for 64 bits), r6-r13 callee-saved, r0 the stack pointer. A call
// pushes the return address, so a function starts with it at [r0].
class ABISysV_rx : public lldb_private::RegInfoBasedABI {
public:
  ~ABISysV_rx() override = default;

  size_t GetRedZoneSize() const override { return 0; }

  bool PrepareTrivialCall(lldb_private::Thread &thread, lldb::addr_t sp,
                          lldb::addr_t functionAddress,
                          lldb::addr_t returnAddress,
                          llvm::ArrayRef<lldb::addr_t> args) const override {
    return false;
  }

  bool GetArgumentValues(lldb_private::Thread &thread,
                         lldb_private::ValueList &values) const override {
    return false;
  }

  lldb_private::Status
  SetReturnValueObject(lldb::StackFrameSP &frame_sp,
                       lldb::ValueObjectSP &new_value) override;

  lldb::ValueObjectSP
  GetReturnValueObjectImpl(lldb_private::Thread &thread,
                           lldb_private::CompilerType &type) const override;

  lldb::UnwindPlanSP CreateFunctionEntryUnwindPlan() override;

  lldb::UnwindPlanSP CreateDefaultUnwindPlan() override;

  bool RegisterIsVolatile(const lldb_private::RegisterInfo *reg_info) override;

  bool CallFrameAddressIsValid(lldb::addr_t cfa) override {
    // The stack is 4-byte aligned.
    return cfa != 0 && (cfa & 3) == 0;
  }

  bool CodeAddressIsValid(lldb::addr_t pc) override { return pc <= UINT32_MAX; }

  const lldb_private::RegisterInfo *
  GetRegisterInfoArray(uint32_t &count) override;

  static void Initialize();

  static void Terminate();

  static lldb::ABISP CreateInstance(lldb::ProcessSP process_sp,
                                    const lldb_private::ArchSpec &arch);

  static llvm::StringRef GetPluginNameStatic() { return "sysv-rx"; }

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

private:
  using lldb_private::RegInfoBasedABI::RegInfoBasedABI;
};

#endif // LLDB_SOURCE_PLUGINS_ABI_RX_ABISYSV_RX_H
