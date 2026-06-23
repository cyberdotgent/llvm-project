//===-- OS400MITargetMachine.cpp - OS/400 MI Target Machine ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file intentionally does not register a TargetMachine yet. OS400MI starts
// as a target-info and Clang data-layout stub while the symbolic MI source
// representation and ABI are defined.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/Compiler.h"

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeOS400MITarget() {}
