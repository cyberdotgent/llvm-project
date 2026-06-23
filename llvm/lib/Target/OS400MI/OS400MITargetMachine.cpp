//===-- OS400MITargetMachine.cpp - OS/400 MI Target Machine ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the first narrow OS400MI code generation path. It emits
// symbolic MI source text directly for a deliberately tiny LLVM IR subset.
//
//===----------------------------------------------------------------------===//

#include "TargetInfo/OS400MITargetInfo.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Pass.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Triple.h"
#include <cstdint>
#include <optional>

using namespace llvm;

namespace {

class OS400MITargetMachine : public TargetMachine {
public:
  OS400MITargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                       StringRef FS, const TargetOptions &Options,
                       std::optional<Reloc::Model> RM,
                       std::optional<CodeModel::Model> CM, CodeGenOptLevel OL,
                       bool JIT)
      : TargetMachine(T, TT.computeDataLayout(), TT, CPU, FS, Options) {
    this->RM = RM.value_or(Reloc::Static);
    this->CMModel = CM.value_or(CodeModel::Small);
    this->OptLevel = OL;
  }

  bool addPassesToEmitFile(PassManagerBase &PM, raw_pwrite_stream &Out,
                           raw_pwrite_stream *DwoOut, CodeGenFileType FileType,
                           bool DisableVerify,
                           MachineModuleInfoWrapperPass *MMIWP) override;

  bool addPassesToEmitMC(PassManagerBase &PM, MCContext *&Ctx,
                         raw_pwrite_stream &Out,
                         bool DisableVerify) override {
    return true;
  }
};

class OS400MIEmitPass : public ModulePass {
  raw_ostream &OS;

  static void fail(const Twine &Message) {
    report_fatal_error("OS400MI MVP only supports " + Message, false);
  }

  static const ReturnInst *getOnlyReturn(const Function &F) {
    if (F.empty())
      fail("a defined i32 main function");
    if (F.size() != 1)
      fail("main with one basic block returning an i32 constant");

    const BasicBlock &BB = F.front();
    if (BB.size() != 1)
      fail("main with a single ret instruction");

    const auto *RI = dyn_cast<ReturnInst>(&BB.front());
    if (!RI)
      fail("main with a single ret instruction");
    return RI;
  }

  static int32_t getMainReturnConstant(const Module &M) {
    const Function *Main = M.getFunction("main");
    if (!Main || Main->isDeclaration())
      fail("a defined i32 main function");

    FunctionType *MainTy = Main->getFunctionType();
    if (!MainTy->getReturnType()->isIntegerTy(32) ||
        MainTy->getNumParams() != 0)
      fail("int main(void)");

    for (const Function &F : M.functions()) {
      if (!F.isDeclaration() && &F != Main)
        fail("only one defined function named main");
    }

    const ReturnInst *RI = getOnlyReturn(*Main);
    const auto *CI = dyn_cast_or_null<ConstantInt>(RI->getReturnValue());
    if (!CI || !CI->getType()->isIntegerTy(32))
      fail("main returning an i32 constant");

    return static_cast<int32_t>(CI->getSExtValue());
  }

  void emitProgram(int32_t MainReturn) {
    OS << "DCL     SPCPTR      ARGC@      PARM;\n";
    OS << "DCL     SPCPTR      ARGV@      PARM;\n";
    OS << "DCL     OL          PARM_LIST\n";
    OS << "                   (ARGC@,\n";
    OS << "                    ARGV@)\n";
    OS << "                    PARM       EXT        MIN(0);\n";
    OS << "DCL     DD          ARGC       BIN(4)     BAS(ARGC@);\n";
    OS << "DCL     SPCPTR      ARGV       BAS(ARGV@);\n";
    OS << "DCL     DD          NBR_PARMS  BIN(2);\n";
    OS << "DCL     DD          MAIN_RC    BIN(4);\n";
    OS << "DCL     INSPTR      .MAIN;\n";
    OS << "ENTRY * (PARM_LIST) EXT;\n";
    OS << "        STPLLEN     NBR_PARMS;\n";
    OS << "        CALLI       MAIN, *, .MAIN;\n";
    OS << "        RTX         *;\n";
    OS << "ENTRY MAIN INT;\n";
    OS << "        CPYNV       MAIN_RC," << MainReturn << ";\n";
    OS << "        B           .MAIN;\n";
    OS << "        PEND;\n";
  }

public:
  static char ID;

  explicit OS400MIEmitPass(raw_ostream &OS) : ModulePass(ID), OS(OS) {}

  StringRef getPassName() const override { return "OS400MI MI Source Emitter"; }

  bool runOnModule(Module &M) override {
    emitProgram(getMainReturnConstant(M));
    return false;
  }
};

char OS400MIEmitPass::ID = 0;

} // end anonymous namespace

bool OS400MITargetMachine::addPassesToEmitFile(
    PassManagerBase &PM, raw_pwrite_stream &Out, raw_pwrite_stream *DwoOut,
    CodeGenFileType FileType, bool DisableVerify,
    MachineModuleInfoWrapperPass *MMIWP) {
  switch (FileType) {
  case CodeGenFileType::AssemblyFile:
  case CodeGenFileType::ObjectFile:
    PM.add(new OS400MIEmitPass(Out));
    return false;
  case CodeGenFileType::Null:
    return false;
  }
  return true;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeOS400MITarget() {
  RegisterTargetMachine<OS400MITargetMachine> X(getTheOS400MITarget());
}
