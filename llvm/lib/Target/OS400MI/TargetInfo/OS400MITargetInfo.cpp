//===-- OS400MITargetInfo.cpp - OS/400 MI Target Impl ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TargetInfo/OS400MITargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
using namespace llvm;

Target &llvm::getTheOS400MITarget() {
  static Target TheOS400MITarget;
  return TheOS400MITarget;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeOS400MITargetInfo() {
  RegisterTarget<Triple::os400mi> X(getTheOS400MITarget(), "os400mi",
                                    "OS/400 symbolic Machine Interface",
                                    "OS400MI");
}
