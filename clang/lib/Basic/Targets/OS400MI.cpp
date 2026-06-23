//===--- OS400MI.cpp - Implement OS/400 MI target support -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "OS400MI.h"
#include "clang/Basic/MacroBuilder.h"

using namespace clang;
using namespace clang::targets;

void OS400MITargetInfo::getTargetDefines(const LangOptions &Opts,
                                         MacroBuilder &Builder) const {
  Builder.defineMacro("__OS400MI__");
  Builder.defineMacro("__os400mi__");
  Builder.defineMacro("__OS400__");
  Builder.defineMacro("__os400__");
}
