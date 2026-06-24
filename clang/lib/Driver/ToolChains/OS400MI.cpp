//===--- OS400MI.cpp - OS/400 MI ToolChain ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "OS400MI.h"

#include "clang/Basic/DiagnosticDriver.h"
#include "clang/Driver/CommonArgs.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/InputInfo.h"
#include "clang/Options/Options.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace clang::driver::tools;
using namespace llvm::opt;

static void addIfExists(const ArgList &DriverArgs, ArgStringList &CC1Args,
                        llvm::StringRef Path) {
  if (!llvm::sys::fs::exists(Path))
    return;
  ToolChain::addSystemInclude(DriverArgs, CC1Args, Path);
}

static void addOS400MISysRootIncludePaths(const ToolChain &TC,
                                          const ArgList &DriverArgs,
                                          ArgStringList &CC1Args) {
  llvm::SmallVector<std::string, 4> IncludePaths;
  std::string SysRoot = TC.computeSysRoot();
  if (SysRoot.empty())
    return;

  llvm::SmallString<256> TooldirInclude(SysRoot);
  llvm::sys::path::append(TooldirInclude, "usr", "local", "os400mi-ibm-os400",
                          "include");
  IncludePaths.push_back(std::string(TooldirInclude));

  llvm::SmallString<256> RootInclude(SysRoot);
  llvm::sys::path::append(RootInclude, "include");
  IncludePaths.push_back(std::string(RootInclude));

  for (const std::string &Path : IncludePaths)
    addIfExists(DriverArgs, CC1Args, Path);
}

static std::string getOS400MIArchiveName(llvm::StringRef LibName) {
  llvm::SmallString<128> FileName;
  if (LibName.starts_with(":")) {
    FileName = LibName.drop_front();
  } else {
    FileName = "lib";
    FileName += LibName;
    FileName += ".a";
  }
  return FileName.str().str();
}

static std::optional<std::string> findOS400MIArchive(const ToolChain &TC,
                                                     const ArgList &Args,
                                                     llvm::StringRef LibName) {
  llvm::SmallVector<std::string, 8> SearchPaths;
  for (std::string Path : Args.getAllArgValues(clang::options::OPT_L))
    SearchPaths.push_back(std::move(Path));
  for (llvm::StringRef Path : TC.getFilePaths())
    SearchPaths.push_back(Path.str());

  std::string FileName = getOS400MIArchiveName(LibName);

  for (llvm::StringRef SearchPath : SearchPaths) {
    llvm::SmallString<256> Candidate(SearchPath);
    llvm::sys::path::append(Candidate, FileName);
    if (llvm::sys::fs::exists(Candidate))
      return std::string(Candidate);
  }

  if (llvm::sys::fs::exists(FileName))
    return std::string(FileName);
  return std::nullopt;
}

void os400mi::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                   const InputInfo &Output,
                                   const InputInfoList &Inputs,
                                   const ArgList &Args,
                                   const char *LinkingOutput) const {
  assert(!Inputs.empty() && "Must have at least one input.");

  ArgStringList LinkArgs;
  for (const InputInfo &Input : Inputs) {
    if (Input.isFilename()) {
      LinkArgs.push_back(Input.getFilename());
      continue;
    }

    if (Input.isInputArg()) {
      if (Input.getInputArg().getOption().matches(options::OPT_l)) {
        llvm::StringRef LibName = Input.getInputArg().getValue();
        if (std::optional<std::string> Archive =
                findOS400MIArchive(getToolChain(), Args, LibName)) {
          LinkArgs.push_back(Args.MakeArgString(*Archive));
          continue;
        }
        C.getDriver().Diag(diag::err_drv_no_such_file)
            << Args.MakeArgString(getOS400MIArchiveName(LibName));
      } else {
        C.getDriver().Diag(diag::err_drv_unsupported_opt)
            << Input.getInputArg().getAsString(Args);
      }
    }
  }

  if (LinkArgs.empty())
    return;

  std::string Stem = std::string(llvm::sys::path::stem(Output.getFilename()));
  const char *LinkedBC =
      C.getDriver().CreateTempFile(C, Stem + "-os400mi-link", "bc", false);

  tools::constructLLVMLinkCommand(C, *this, JA, Inputs, LinkArgs, Output, Args,
                                  LinkedBC);

  ArgStringList LlcArgs;
  LlcArgs.push_back(
      Args.MakeArgString("-mtriple=" + getToolChain().getTripleString()));
  LlcArgs.push_back("-filetype=asm");
  LlcArgs.push_back("-o");
  LlcArgs.push_back(Output.getFilename());
  LlcArgs.push_back(LinkedBC);

  const char *Llc = Args.MakeArgString(getToolChain().GetProgramPath("llc"));
  InputInfo LinkedInput(types::TY_LLVM_BC, LinkedBC, "");
  C.addCommand(std::make_unique<Command>(JA, *this, ResponseFileSupport::None(),
                                         Llc, LlcArgs, LinkedInput, Output));
}

OS400MI::OS400MI(const Driver &D, const llvm::Triple &Triple,
                 const ArgList &Args)
    : ToolChain(D, Triple, Args) {
  getProgramPaths().push_back(getDriver().Dir);

  std::string SysRoot = computeSysRoot();
  if (!SysRoot.empty()) {
    llvm::SmallString<256> LibDir(SysRoot);
    llvm::sys::path::append(LibDir, "usr", "local", "os400mi-ibm-os400",
                            "lib");
    if (llvm::sys::fs::exists(LibDir))
      getFilePaths().push_back(std::string(LibDir));

    llvm::SmallString<256> RootLib(SysRoot);
    llvm::sys::path::append(RootLib, "lib");
    if (llvm::sys::fs::exists(RootLib))
      getFilePaths().push_back(std::string(RootLib));
  }
}

void OS400MI::AddClangSystemIncludeArgs(const ArgList &DriverArgs,
                                        ArgStringList &CC1Args) const {
  addOS400MISysRootIncludePaths(*this, DriverArgs, CC1Args);
}

LTOKind OS400MI::getLTOMode(const ArgList &Args,
                            Action::OffloadKind Kind) const {
  if (Kind != Action::OFK_None)
    return ToolChain::getLTOMode(Args, Kind);
  return LTOK_Full;
}

Tool *OS400MI::buildLinker() const { return new os400mi::Linker(*this); }
