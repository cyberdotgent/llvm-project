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
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Pass.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/TargetParser/Triple.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

using namespace llvm;

namespace {

class OS400MITargetMachine : public TargetMachine {
  std::unique_ptr<TargetLoweringObjectFile> TLOF;

public:
  OS400MITargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                       StringRef FS, const TargetOptions &Options,
                       std::optional<Reloc::Model> RM,
                       std::optional<CodeModel::Model> CM, CodeGenOptLevel OL,
                       bool JIT)
      : TargetMachine(T, TT.computeDataLayout(), TT, CPU, FS, Options),
        TLOF(std::make_unique<TargetLoweringObjectFileELF>()) {
    this->RM = RM.value_or(Reloc::Static);
    this->CMModel = CM.value_or(CodeModel::Small);
    this->OptLevel = OL;
    this->MRI.reset(T.createMCRegInfo(TT));
    this->MII.reset(T.createMCInstrInfo());
    this->STI.reset(T.createMCSubtargetInfo(TT, CPU, FS));
    this->AsmInfo.reset(T.createMCAsmInfo(*MRI, TT, Options.MCOptions));
  }

  TargetLoweringObjectFile *getObjFileLowering() const override {
    return TLOF.get();
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

  static constexpr uint32_t ArenaSize = 256;
  static constexpr uint32_t FirstArenaOffset = 4;

  static void fail(const Twine &Message) {
    report_fatal_error("OS400MI MVP only supports " + Message, false);
  }

  static const Function &getMainFunction(const Module &M) {
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

    if (Main->empty())
      fail("a defined i32 main function");
    if (Main->size() != 1)
      fail("main with one basic block");

    return *Main;
  }

  class NameAllocator {
    unsigned NextSlot = 1;
    unsigned NextTemp = 1;

  public:
    std::string createSlotName() {
      return formatv("S{0,0+6}", NextSlot++).str();
    }

    std::string createTempName() {
      return formatv("T{0,0+6}", NextTemp++).str();
    }
  };

  struct ArenaSlot {
    std::string Name;
    uint32_t Offset = 0;
  };

  class FunctionLowerer {
    NameAllocator Names;
    DenseMap<const Value *, std::string> Values;
    DenseMap<const AllocaInst *, ArenaSlot> Slots;
    SmallVector<std::string, 8> Declarations;
    SmallVector<std::string, 16> Body;
    uint32_t NextArenaOffset = FirstArenaOffset;

    static bool isI32(Type *Ty) { return Ty && Ty->isIntegerTy(32); }

    static uint32_t alignTo4(uint32_t Value) { return (Value + 3) & ~3U; }

    std::string getOperandName(const Value *V) const {
      if (const auto *CI = dyn_cast<ConstantInt>(V)) {
        if (!CI->getType()->isIntegerTy(32))
          fail("i32 constants in lowered expressions");
        return std::to_string(static_cast<int32_t>(CI->getSExtValue()));
      }

      auto It = Values.find(V);
      if (It == Values.end())
        fail("operands defined by previous supported i32 instructions");
      return It->second;
    }

    const ArenaSlot &getAllocaSlot(const Value *V) const {
      const auto *AI = dyn_cast<AllocaInst>(V);
      if (!AI)
        fail("loads and stores through direct i32 allocas");

      auto It = Slots.find(AI);
      if (It == Slots.end())
        fail("loads and stores through direct i32 allocas");
      return It->second;
    }

    std::string createTemp() {
      std::string Name = Names.createTempName();
      Declarations.push_back("DCL     DD          " + Name + "    BIN(4);");
      return Name;
    }

    void lowerAlloca(const AllocaInst &AI) {
      if (!isI32(AI.getAllocatedType()))
        fail("i32 allocas");
      if (AI.isArrayAllocation())
        fail("scalar i32 allocas");

      uint32_t Offset = alignTo4(NextArenaOffset);
      if (Offset + 4 > ArenaSize)
        fail("phase-1 arena storage within 256 bytes");

      ArenaSlot Slot{Names.createSlotName(), Offset};
      NextArenaOffset = Offset + 4;
      Slots[&AI] = Slot;
      Declarations.push_back("DCL     DD          " + Slot.Name +
                             "    BIN(4)     DEF(C_MEM) POS(" +
                             std::to_string(Slot.Offset + 1) + ");");
    }

    void lowerBinaryOperator(const BinaryOperator &BO) {
      if (!isI32(BO.getType()))
        fail("i32 add/sub expressions");

      std::string Dest = createTemp();
      std::string LHS = getOperandName(BO.getOperand(0));
      std::string RHS = getOperandName(BO.getOperand(1));

      switch (BO.getOpcode()) {
      case Instruction::Add:
        Body.push_back("        ADDN        " + Dest + "," + LHS + "," + RHS +
                       ";");
        break;
      case Instruction::Sub:
        Body.push_back("        SUBN        " + Dest + "," + LHS + "," + RHS +
                       ";");
        break;
      default:
        fail("i32 add/sub expressions");
      }

      Values[&BO] = Dest;
    }

    void lowerStore(const StoreInst &SI) {
      if (!isI32(SI.getValueOperand()->getType()))
        fail("i32 stores");
      const ArenaSlot &Slot = getAllocaSlot(SI.getPointerOperand());
      Body.push_back("        CPYNV       " + Slot.Name + "," +
                     getOperandName(SI.getValueOperand()) + ";");
    }

    void lowerLoad(const LoadInst &LI) {
      if (!isI32(LI.getType()))
        fail("i32 loads");
      const ArenaSlot &Slot = getAllocaSlot(LI.getPointerOperand());
      std::string Dest = createTemp();
      Body.push_back("        CPYNV       " + Dest + "," + Slot.Name + ";");
      Values[&LI] = Dest;
    }

    void lowerReturn(const ReturnInst &RI) {
      Value *Ret = RI.getReturnValue();
      if (!Ret || !isI32(Ret->getType()))
        fail("i32 returns from main");
      Body.push_back("        CPYNV       MAIN_RC," + getOperandName(Ret) + ";");
      Body.push_back("        B           .MAIN;");
    }

  public:
    void lower(const Function &F) {
      const BasicBlock &BB = F.front();
      bool SawReturn = false;

      for (const Instruction &I : BB) {
        if (SawReturn)
          fail("instructions before a single final ret");

        if (const auto *AI = dyn_cast<AllocaInst>(&I)) {
          lowerAlloca(*AI);
        } else if (const auto *BO = dyn_cast<BinaryOperator>(&I)) {
          lowerBinaryOperator(*BO);
        } else if (const auto *SI = dyn_cast<StoreInst>(&I)) {
          lowerStore(*SI);
        } else if (const auto *LI = dyn_cast<LoadInst>(&I)) {
          lowerLoad(*LI);
        } else if (const auto *RI = dyn_cast<ReturnInst>(&I)) {
          lowerReturn(*RI);
          SawReturn = true;
        } else {
          fail("i32 alloca/store/load/add/sub and ret instructions");
        }
      }

      if (!SawReturn)
        fail("main ending in ret i32");
    }

    ArrayRef<std::string> getDeclarations() const { return Declarations; }
    ArrayRef<std::string> getBody() const { return Body; }
  };

  void emitProgram(const FunctionLowerer &Lowerer) {
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
    OS << "DCL     DD          C_MEM      CHAR(" << ArenaSize << ") BDRY(16);\n";
    OS << "DCL     SPCPTR      .C_BASE    INIT(C_MEM);\n";
    for (const std::string &Decl : Lowerer.getDeclarations())
      OS << Decl << "\n";
    OS << "DCL     INSPTR      .MAIN;\n";
    OS << "ENTRY * (PARM_LIST) EXT;\n";
    OS << "        STPLLEN     NBR_PARMS;\n";
    OS << "        CALLI       MAIN, *, .MAIN;\n";
    OS << "        RTX         *;\n";
    OS << "ENTRY MAIN INT;\n";
    for (const std::string &Line : Lowerer.getBody())
      OS << Line << "\n";
    OS << "        PEND;\n";
  }

public:
  static char ID;

  explicit OS400MIEmitPass(raw_ostream &OS) : ModulePass(ID), OS(OS) {}

  StringRef getPassName() const override { return "OS400MI MI Source Emitter"; }

  bool runOnModule(Module &M) override {
    const Function &Main = getMainFunction(M);
    FunctionLowerer Lowerer;
    Lowerer.lower(Main);
    emitProgram(Lowerer);
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
