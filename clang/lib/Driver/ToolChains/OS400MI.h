//===--- OS400MI.h - OS/400 MI ToolChain ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_OS400MI_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_OS400MI_H

#include "clang/Driver/Driver.h"
#include "clang/Driver/Tool.h"
#include "clang/Driver/ToolChain.h"

namespace clang {
namespace driver {

namespace tools {
namespace os400mi {

class LLVM_LIBRARY_VISIBILITY Linker final : public Tool {
public:
  Linker(const ToolChain &TC) : Tool("os400mi::Linker", "llvm-link", TC) {}

  bool hasIntegratedCPP() const override { return false; }
  bool isLinkJob() const override { return true; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &Args,
                    const char *LinkingOutput) const override;
};

} // namespace os400mi
} // namespace tools

namespace toolchains {

class LLVM_LIBRARY_VISIBILITY OS400MI final : public ToolChain {
public:
  OS400MI(const Driver &D, const llvm::Triple &Triple,
          const llvm::opt::ArgList &Args);

  bool IsIntegratedBackendDefault() const override { return true; }
  bool IsNonIntegratedBackendSupported() const override { return false; }
  bool HasNativeLLVMSupport() const override { return true; }
  bool isCrossCompiling() const override { return true; }
  bool isPICDefault() const override { return false; }
  bool isPIEDefault(const llvm::opt::ArgList &Args) const override {
    return false;
  }
  bool isPICDefaultForced() const override { return false; }
  bool SupportsProfiling() const override { return false; }
  void AddClangSystemIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                                 llvm::opt::ArgStringList &CC1Args) const override;
  LTOKind getDefaultLTOMode() const override { return LTOK_Full; }
  LTOKind getLTOMode(const llvm::opt::ArgList &Args,
                     Action::OffloadKind Kind = Action::OFK_None) const override;
  RuntimeLibType GetDefaultRuntimeLibType() const override {
    return ToolChain::RLT_CompilerRT;
  }
  CStdlibType GetCStdlibType(const llvm::opt::ArgList &Args) const override {
    return ToolChain::CST_Newlib;
  }
  UnwindLibType GetDefaultUnwindLibType() const override {
    return ToolChain::UNW_None;
  }

protected:
  Tool *buildLinker() const override;
};

} // namespace toolchains
} // namespace driver
} // namespace clang

#endif
