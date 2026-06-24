//===--- OS400MI.cpp - Implement OS/400 MI target support -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "OS400MI.h"
#include "clang/Basic/MacroBuilder.h"
#include "clang/Basic/TargetBuiltins.h"

using namespace clang;
using namespace clang::targets;

static constexpr int NumBuiltins =
    clang::OS400MI::LastTSBuiltin - Builtin::FirstTSBuiltin;

#define GET_BUILTIN_STR_TABLE
#include "clang/Basic/BuiltinsOS400MI.inc"
#undef GET_BUILTIN_STR_TABLE

static constexpr Builtin::Info BuiltinInfos[] = {
#define GET_BUILTIN_INFOS
#include "clang/Basic/BuiltinsOS400MI.inc"
#undef GET_BUILTIN_INFOS
};
static_assert(std::size(BuiltinInfos) == NumBuiltins);

void OS400MITargetInfo::getTargetDefines(const LangOptions &Opts,
                                         MacroBuilder &Builder) const {
  (void)Opts;
  Builder.defineMacro("__OS400MI__");
  Builder.defineMacro("__os400mi__");
  Builder.defineMacro("__OS400__");
  Builder.defineMacro("__os400__");
  Builder.defineMacro("__SCHAR_WIDTH__", "8");
  Builder.defineMacro("__UCHAR_WIDTH__", "8");
  Builder.defineMacro("__SHRT_WIDTH__", Twine(getShortWidth()));
  Builder.defineMacro("__USHRT_WIDTH__", Twine(getShortWidth()));
  Builder.defineMacro("__INT_WIDTH__", Twine(getIntWidth()));
  Builder.defineMacro("__UINT_WIDTH__", Twine(getIntWidth()));
  Builder.defineMacro("__LONG_WIDTH__", Twine(getLongWidth()));
  Builder.defineMacro("__ULONG_WIDTH__", Twine(getLongWidth()));
  Builder.defineMacro("__LLONG_WIDTH__", Twine(getLongLongWidth()));
  Builder.defineMacro("__LONG_LONG_WIDTH__", Twine(getLongLongWidth()));
  Builder.defineMacro("__ULLONG_WIDTH__", Twine(getLongLongWidth()));
}

llvm::SmallVector<Builtin::InfosShard>
OS400MITargetInfo::getTargetBuiltins() const {
  return {{&BuiltinStrings, BuiltinInfos}};
}
