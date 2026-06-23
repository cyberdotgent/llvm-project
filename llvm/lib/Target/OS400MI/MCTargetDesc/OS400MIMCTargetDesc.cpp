//===-- OS400MIMCTargetDesc.cpp - OS/400 MI Target Descriptions -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The current OS400MI backend emits symbolic MI source from a custom module
// pass, so it does not use LLVM's MC layer yet. LLVM tools still call every
// configured target's TargetMC initializer, so provide the initializer as the
// future home for MC registrations.
//
//===----------------------------------------------------------------------===//

#include "TargetInfo/OS400MITargetInfo.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

namespace {

class OS400MIMCAsmInfo : public MCAsmInfo {
public:
  explicit OS400MIMCAsmInfo(const Triple &TT, const MCTargetOptions &Options)
      : MCAsmInfo(Options) {}
};

MCInstrInfo *createOS400MIMCInstrInfo() { return new MCInstrInfo(); }

MCRegisterInfo *createOS400MIMCRegisterInfo(const Triple &TT) {
  return new MCRegisterInfo();
}

MCSubtargetInfo *createOS400MIMCSubtargetInfo(const Triple &TT, StringRef CPU,
                                              StringRef FS) {
  return new MCSubtargetInfo(TT, CPU, /*TuneCPU=*/CPU, FS, {}, {}, {}, nullptr,
                             nullptr, nullptr, nullptr, nullptr, nullptr);
}

} // end anonymous namespace

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeOS400MITargetMC() {
  Target &T = getTheOS400MITarget();
  RegisterMCAsmInfo<OS400MIMCAsmInfo> X(T);
  TargetRegistry::RegisterMCInstrInfo(T, createOS400MIMCInstrInfo);
  TargetRegistry::RegisterMCRegInfo(T, createOS400MIMCRegisterInfo);
  TargetRegistry::RegisterMCSubtargetInfo(T, createOS400MIMCSubtargetInfo);
}
