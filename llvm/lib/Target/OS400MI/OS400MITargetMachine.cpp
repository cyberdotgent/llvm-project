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
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
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
#include "llvm/Support/Format.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <set>
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
  std::string OutputFilename;

  static constexpr uint32_t ArenaSize = 256;
  static constexpr uint32_t FirstArenaOffset = 4;

  static void fail(const Twine &Message) {
    report_fatal_error("OS400MI MVP only supports " + Message, false);
  }

  static void writeJSONString(raw_ostream &OS, StringRef S) {
    OS << '"';
    for (char C : S) {
      switch (C) {
      case '"':
        OS << "\\\"";
        break;
      case '\\':
        OS << "\\\\";
        break;
      case '\b':
        OS << "\\b";
        break;
      case '\f':
        OS << "\\f";
        break;
      case '\n':
        OS << "\\n";
        break;
      case '\r':
        OS << "\\r";
        break;
      case '\t':
        OS << "\\t";
        break;
      default:
        if (static_cast<unsigned char>(C) < 0x20)
          OS << formatv("\\u{0,0+4:X}", static_cast<unsigned char>(C));
        else
          OS << C;
        break;
      }
    }
    OS << '"';
  }

  static void appendRawAsmLines(SmallVectorImpl<std::string> &Output,
                                StringRef Asm, bool RejectEmpty = false) {
    SmallVector<StringRef, 8> Lines;
    Asm.split(Lines, '\n');
    for (StringRef Line : Lines) {
      Line = Line.rtrim("\r");
      if (Line.empty()) {
        if (RejectEmpty)
          fail("non-empty OS400MI inline asm lines");
        continue;
      }
      Output.push_back(Line.str());
    }
  }

  static void validateRawInlineAsm(const CallBase &CB) {
    const auto *IA = dyn_cast<InlineAsm>(CB.getCalledOperand());
    if (!IA)
      fail("inline asm call operand");
    if (!CB.getType()->isVoidTy() || CB.arg_size() != 0)
      fail("constraint-free void OS400MI inline asm");
    if (!IA->getConstraintString().empty())
      fail("constraint-free OS400MI inline asm");
    if (IA->canThrow())
      fail("non-unwinding OS400MI inline asm");
    if (isa<CallBrInst>(CB))
      fail("OS400MI inline asm without asm-goto labels");
  }

  struct SourceLocationRecord {
    std::string File;
    std::optional<unsigned> Line;
    std::optional<unsigned> Column;
  };

  struct GeneratedName {
    std::string Name;
    std::string Class;
    uint32_t Ordinal = 0;
    uint32_t CollisionOrdinal = 0;
    bool Collision = false;
    std::string Hash;
  };

  class NameAllocator {
    static constexpr unsigned MaxMINameLength = 48;

    std::set<std::string> UsedNames;
    unsigned NextSlot = 1;
    unsigned NextTemp = 1;
    unsigned NextBlock = 1;
    unsigned NextFunction = 1;
    unsigned NextGlobal = 1;
    unsigned NextLiteral = 1;
    unsigned NextHelper = 1;

    static std::string formatOrdinalName(StringRef Prefix, unsigned Ordinal) {
      return formatv("{0}{1,0+6}", Prefix, Ordinal).str();
    }

    static std::string getHashMaterial(StringRef Class, StringRef Original) {
      if (Original.empty())
        return "";

      std::string Material = Class.str();
      Material.push_back('\0');
      Material.append(Original.str());
      uint32_t Hash = static_cast<uint32_t>(hash_value(StringRef(Material)));
      std::string Hex;
      raw_string_ostream OS(Hex);
      OS << format_hex_no_prefix(Hash, 8, true);
      return OS.str();
    }

    unsigned &getCounter(StringRef Class) {
      if (Class == "local")
        return NextSlot;
      if (Class == "temp")
        return NextTemp;
      if (Class == "label")
        return NextBlock;
      if (Class == "function")
        return NextFunction;
      if (Class == "global")
        return NextGlobal;
      if (Class == "literal")
        return NextLiteral;
      if (Class == "helper")
        return NextHelper;
      fail("known generated name class");
      llvm_unreachable("fail should not return");
    }

    GeneratedName allocate(StringRef Class, StringRef Prefix,
                           StringRef Original = "") {
      unsigned &Next = getCounter(Class);
      unsigned Ordinal = Next++;
      std::string Hash = getHashMaterial(Class, Original);
      std::string Name = formatOrdinalName(Prefix, Ordinal);
      if (Name.size() > MaxMINameLength)
        fail("generated MI names no longer than 48 characters");

      uint32_t CollisionOrdinal = 0;
      bool Collision = !UsedNames.insert(Name).second;
      while (Collision) {
        ++CollisionOrdinal;
        Name = formatv("{0}{1,0+6}_{2}_{3}", Prefix, Ordinal,
                       Hash.empty() ? "00000000" : Hash, CollisionOrdinal)
                   .str();
        if (Name.size() > MaxMINameLength)
          fail("generated MI collision names no longer than 48 characters");
        if (UsedNames.insert(Name).second)
          break;
      }

      return {std::move(Name), Class.str(), Ordinal, CollisionOrdinal,
              Collision, std::move(Hash)};
    }

  public:
    static constexpr unsigned getMaxMINameLength() { return MaxMINameLength; }

    void reserveName(StringRef Name) {
      if (Name.size() > MaxMINameLength)
        fail("reserved MI names no longer than 48 characters");
      UsedNames.insert(Name.str());
    }

    GeneratedName createSlotName(StringRef Original = "") {
      return allocate("local", "S", Original);
    }

    GeneratedName createTempName(StringRef Original = "") {
      return allocate("temp", "T", Original);
    }

    GeneratedName createBlockName(StringRef Original = "") {
      return allocate("label", "B", Original);
    }

    GeneratedName createFunctionName(StringRef Original = "") {
      return allocate("function", "F", Original);
    }

    GeneratedName createGlobalName(StringRef Original = "") {
      return allocate("global", "G", Original);
    }

    GeneratedName createLiteralName(StringRef Original = "") {
      return allocate("literal", "L", Original);
    }

    GeneratedName createHelperName(StringRef Original = "") {
      return allocate("helper", "H", Original);
    }
  };

  enum class AccessWidth : uint8_t { I8 = 1, I16 = 2, I32 = 4, I64 = 8 };

  static AccessWidth getIntegerAccessWidth(Type *Ty) {
    if (Ty->isIntegerTy(8))
      return AccessWidth::I8;
    if (Ty->isIntegerTy(16))
      return AccessWidth::I16;
    if (Ty->isIntegerTy(32))
      return AccessWidth::I32;
    if (Ty->isIntegerTy(64))
      return AccessWidth::I64;
    fail("i8/i16/i32/i64 arena accesses");
    llvm_unreachable("fail should not return");
  }

  static uint32_t getAccessWidthBytes(AccessWidth Width) {
    return static_cast<uint32_t>(Width);
  }

  static uint32_t getABIAlignment(const DataLayout &DL, Type *Ty) {
    return static_cast<uint32_t>(DL.getABITypeAlign(Ty).value());
  }

  static void storeIntegerBytes(const APInt &Value, uint64_t Size,
                                MutableArrayRef<uint8_t> Dest) {
    APInt Bytes = Value.zextOrTrunc(Size * 8);
    for (uint64_t I = 0; I != Size; ++I)
      Dest[I] = static_cast<uint8_t>(
          Bytes.lshr((Size - I - 1) * 8).getZExtValue() & 0xFF);
  }

  static bool isSupportedArenaAggregateType(Type *Ty) {
    if (Ty->isIntegerTy(8) || Ty->isIntegerTy(16) ||
        Ty->isIntegerTy(32) || Ty->isIntegerTy(64) || Ty->isPointerTy())
      return true;

    if (const auto *AT = dyn_cast<ArrayType>(Ty))
      return isSupportedArenaAggregateType(AT->getElementType());

    if (const auto *ST = dyn_cast<StructType>(Ty)) {
      for (Type *EltTy : ST->elements()) {
        if (!isSupportedArenaAggregateType(EltTy))
          return false;
      }
      return true;
    }

    return false;
  }

  static std::string getRawHex(ArrayRef<uint8_t> Bytes) {
    const char Digits[] = "0123456789ABCDEF";
    std::string Hex;
    Hex.reserve(Bytes.size() * 2);
    for (uint8_t Byte : Bytes) {
      Hex.push_back(Digits[Byte >> 4]);
      Hex.push_back(Digits[Byte & 0x0F]);
    }
    return Hex;
  }

  static void writeConstantBytes(const DataLayout &DL, const Constant *C,
                                 MutableArrayRef<uint8_t> Bytes) {
    if (isa<UndefValue>(C) || isa<ConstantAggregateZero>(C)) {
      std::fill(Bytes.begin(), Bytes.end(), 0);
      return;
    }

    if (const auto *CI = dyn_cast<ConstantInt>(C)) {
      if (!(CI->getType()->isIntegerTy(8) || CI->getType()->isIntegerTy(16) ||
            CI->getType()->isIntegerTy(32) || CI->getType()->isIntegerTy(64)))
        fail("i8/i16/i32/i64 integer aggregate constants");
      storeIntegerBytes(CI->getValue(), Bytes.size(), Bytes);
      return;
    }

    Type *Ty = C->getType();
    if (auto *AT = dyn_cast<ArrayType>(Ty)) {
      uint64_t EltSize = DL.getTypeAllocSize(AT->getElementType());
      for (uint64_t I = 0, E = AT->getNumElements(); I != E; ++I) {
        const Constant *Elt =
            isa<ConstantAggregateZero>(C) || isa<UndefValue>(C)
                ? Constant::getNullValue(AT->getElementType())
                : C->getAggregateElement(static_cast<unsigned>(I));
        if (!Elt)
          fail("constant integer array elements");
        writeConstantBytes(DL, Elt, Bytes.slice(I * EltSize, EltSize));
      }
      return;
    }

    if (auto *ST = dyn_cast<StructType>(Ty)) {
      const StructLayout *Layout = DL.getStructLayout(ST);
      for (unsigned I = 0, E = ST->getNumElements(); I != E; ++I) {
        Type *EltTy = ST->getElementType(I);
        uint64_t EltSize = DL.getTypeAllocSize(EltTy);
        const Constant *Elt =
            isa<ConstantAggregateZero>(C) || isa<UndefValue>(C)
                ? Constant::getNullValue(EltTy)
                : C->getAggregateElement(I);
        if (!Elt)
          fail("constant integer struct elements");
        writeConstantBytes(DL, Elt,
                           Bytes.slice(Layout->getElementOffset(I), EltSize));
      }
      return;
    }

    fail("integer array/struct aggregate constants");
  }

  struct ArenaSlot {
    std::string Name;
    uint32_t Offset = 0;
    uint32_t Size = 0;
    AccessWidth Width = AccessWidth::I32;
    bool DirectScalar = false;
  };

  static bool isSupportedArenaValueType(Type *Ty) {
    return isSupportedArenaAggregateType(Ty);
  }

  static bool isAggregateABIValueType(Type *Ty) {
    return Ty && !(Ty->isIntegerTy(1) || Ty->isIntegerTy(8) ||
                   Ty->isIntegerTy(16) || Ty->isIntegerTy(32) ||
                   Ty->isIntegerTy(64) || Ty->isPointerTy()) &&
           isSupportedArenaValueType(Ty);
  }

  static std::optional<bool>
  getAggregateComparePseudoKind(const Function *F) {
    if (!F)
      return std::nullopt;

    StringRef Name = F->getName();
    std::optional<bool> IsEqual;
    if (Name.starts_with("llvm.os400mi.aggregate.eq"))
      IsEqual = true;
    else if (Name.starts_with("llvm.os400mi.aggregate.ne"))
      IsEqual = false;
    else
      return std::nullopt;

    FunctionType *Ty = F->getFunctionType();
    if (!F->isDeclaration() || !Ty->getReturnType()->isIntegerTy(1) ||
        Ty->getNumParams() != 2 || Ty->getParamType(0) != Ty->getParamType(1) ||
        !isAggregateABIValueType(Ty->getParamType(0)))
      fail("OS400MI aggregate equality pseudo calls with two matching "
           "supported aggregate operands");

    return IsEqual;
  }

  struct CompareValue {
    CmpInst::Predicate Predicate;
    std::string LHS;
    std::string RHS;
  };

  struct I64Value {
    std::string Bytes;
    std::string Hi;
    std::string HiU;
    std::string Lo;
    std::string LoBytes;
    std::array<std::string, 8> Byte;
  };

  static I64Value makeI64Value(StringRef BaseName) {
    I64Value Value;
    Value.Bytes = BaseName.str();
    Value.Hi = (BaseName + "H").str();
    Value.HiU = (BaseName + "U").str();
    Value.Lo = (BaseName + "L").str();
    Value.LoBytes = (BaseName + "LB").str();
    for (unsigned I = 0; I != 8; ++I)
      Value.Byte[I] = formatv("{0}B{1}", BaseName, I + 1).str();
    return Value;
  }

  static void appendI64Declarations(SmallVectorImpl<std::string> &Declarations,
                                    const I64Value &Value) {
    Declarations.push_back("DCL     DD          " + Value.Bytes +
                           "    CHAR(8);");
    Declarations.push_back("DCL     DD          " + Value.Hi +
                           "    BIN(4)     DEF(" + Value.Bytes + ") POS(1);");
    Declarations.push_back("DCL     DD          " + Value.HiU +
                           "    BIN(4)     UNSGND DEF(" + Value.Bytes +
                           ") POS(1);");
    Declarations.push_back("DCL     DD          " + Value.Lo +
                           "    BIN(4)     UNSGND DEF(" + Value.Bytes +
                           ") POS(5);");
    Declarations.push_back("DCL     DD          " + Value.LoBytes +
                           "   CHAR(4)    DEF(" + Value.Bytes + ") POS(5);");
    for (unsigned I = 0; I != 8; ++I)
      Declarations.push_back("DCL     DD          " + Value.Byte[I] +
                             "   CHAR(1)    DEF(" + Value.Bytes + ") POS(" +
                             std::to_string(I + 1) + ");");
  }

  struct I64CompareValue {
    CmpInst::Predicate Predicate;
    I64Value LHS;
    I64Value RHS;
  };

  struct MapRecord {
    std::string MIName;
    std::string Kind;
    std::string NameClass;
    std::string Original;
    std::optional<uint32_t> NameOrdinal;
    std::optional<uint32_t> CollisionOrdinal;
    bool Collision = false;
    std::optional<uint32_t> MaxNameLength;
    std::string Hash;
    std::optional<SourceLocationRecord> SourceLocation;
    std::optional<uint32_t> ArenaOffset;
    std::optional<uint32_t> Size;
    std::optional<uint32_t> Alignment;
    std::string Encoding;
  };

  static SourceLocationRecord getSourceLocationRecord(const DILocation &Loc) {
    SourceLocationRecord Record;
    if (DILocalScope *Scope = Loc.getScope()) {
      if (DIFile *File = Scope->getFile()) {
        Record.File = File->getFilename().str();
        if (Record.File.empty())
          Record.File = File->getDirectory().str();
      }
    }
    Record.Line = Loc.getLine();
    Record.Column = Loc.getColumn();
    return Record;
  }

  static std::optional<SourceLocationRecord>
  getSourceLocation(const Instruction &I) {
    if (DebugLoc Loc = I.getDebugLoc())
      return getSourceLocationRecord(*Loc);
    return std::nullopt;
  }

  static std::optional<SourceLocationRecord>
  getSourceLocation(const BasicBlock &BB) {
    for (const Instruction &I : BB) {
      if (std::optional<SourceLocationRecord> Loc = getSourceLocation(I))
        return Loc;
    }
    return std::nullopt;
  }

  static std::optional<SourceLocationRecord>
  getSourceLocation(const Function &F) {
    if (DISubprogram *SP = F.getSubprogram()) {
      SourceLocationRecord Record;
      if (DIFile *File = SP->getFile())
        Record.File = File->getFilename().str();
      Record.Line = SP->getLine();
      return Record;
    }
    return std::nullopt;
  }

  static std::optional<SourceLocationRecord>
  getSourceLocation(const GlobalVariable &GV) {
    SmallVector<DIGlobalVariableExpression *, 1> GVs;
    GV.getDebugInfo(GVs);
    if (GVs.empty())
      return std::nullopt;

    DIGlobalVariable *DIGV = GVs.front()->getVariable();
    if (!DIGV)
      return std::nullopt;

    SourceLocationRecord Record;
    if (DIFile *File = DIGV->getFile())
      Record.File = File->getFilename().str();
    Record.Line = DIGV->getLine();
    return Record;
  }

  class ArenaLayout {
    struct GlobalObject {
      std::string Name;
      std::string Kind;
      uint32_t Offset = 0;
      uint32_t Size = 0;
      uint32_t Alignment = 1;
      AccessWidth Width = AccessWidth::I32;
      bool DirectScalar = false;
    };

    NameAllocator &Names;
    const DataLayout &DL;
    DenseMap<const GlobalVariable *, GlobalObject> Globals;
    SmallVector<std::string, 8> Declarations;
    SmallVector<MapRecord, 8> MapRecords;
    uint32_t NextArenaOffset = FirstArenaOffset;

    static uint32_t alignTo(uint32_t Value, uint32_t Alignment) {
      return (Value + Alignment - 1) & ~(Alignment - 1);
    }

    static std::string getOriginalName(const GlobalVariable &GV) {
      if (GV.hasName())
        return GV.getName().str();
      return "";
    }

    static uint8_t encodeCP37Byte(uint8_t Byte) {
      if (Byte == 0)
        return 0;

      switch (Byte) {
      case '\t':
        return 0x05;
      case '\n':
        return 0x25;
      case '\r':
        return 0x0D;
      case ' ':
        return 0x40;
      case '!':
        return 0x5A;
      case '"':
        return 0x7F;
      case '#':
        return 0x7B;
      case '$':
        return 0x5B;
      case '%':
        return 0x6C;
      case '&':
        return 0x50;
      case '\'':
        return 0x7D;
      case '(':
        return 0x4D;
      case ')':
        return 0x5D;
      case '*':
        return 0x5C;
      case '+':
        return 0x4E;
      case ',':
        return 0x6B;
      case '-':
        return 0x60;
      case '.':
        return 0x4B;
      case '/':
        return 0x61;
      case ':':
        return 0x7A;
      case ';':
        return 0x5E;
      case '<':
        return 0x4C;
      case '=':
        return 0x7E;
      case '>':
        return 0x6E;
      case '?':
        return 0x6F;
      case '@':
        return 0x7C;
      case '[':
        return 0xBA;
      case '\\':
        return 0xE0;
      case ']':
        return 0xBB;
      case '^':
        return 0xB0;
      case '_':
        return 0x6D;
      case '`':
        return 0x79;
      case '{':
        return 0xC0;
      case '|':
        return 0x4F;
      case '}':
        return 0xD0;
      case '~':
        return 0xA1;
      default:
        break;
      }

      if (Byte >= '0' && Byte <= '9')
        return 0xF0 + (Byte - '0');
      if (Byte >= 'A' && Byte <= 'I')
        return 0xC1 + (Byte - 'A');
      if (Byte >= 'J' && Byte <= 'R')
        return 0xD1 + (Byte - 'J');
      if (Byte >= 'S' && Byte <= 'Z')
        return 0xE2 + (Byte - 'S');
      if (Byte >= 'a' && Byte <= 'i')
        return 0x81 + (Byte - 'a');
      if (Byte >= 'j' && Byte <= 'r')
        return 0x91 + (Byte - 'j');
      if (Byte >= 's' && Byte <= 'z')
        return 0xA2 + (Byte - 's');

      fail("ASCII string literal bytes representable as IBM-037");
      llvm_unreachable("fail should not return");
    }

    static bool canEncodeCP37Byte(uint8_t Byte) {
      if (Byte == 0 || Byte == '\t' || Byte == '\n' || Byte == '\r')
        return true;
      if ((Byte >= '0' && Byte <= '9') || (Byte >= 'A' && Byte <= 'Z') ||
          (Byte >= 'a' && Byte <= 'z'))
        return true;

      switch (Byte) {
      case ' ':
      case '!':
      case '"':
      case '#':
      case '$':
      case '%':
      case '&':
      case '\'':
      case '(':
      case ')':
      case '*':
      case '+':
      case ',':
      case '-':
      case '.':
      case '/':
      case ':':
      case ';':
      case '<':
      case '=':
      case '>':
      case '?':
      case '@':
      case '[':
      case '\\':
      case ']':
      case '^':
      case '_':
      case '`':
      case '{':
      case '|':
      case '}':
      case '~':
        return true;
      default:
        return false;
      }
    }

    static bool isCP37StringLiteral(const ConstantDataArray &CDA) {
      if (!CDA.isCString())
        return false;

      StringRef Bytes = CDA.getAsString();
      return llvm::all_of(Bytes.bytes(), canEncodeCP37Byte);
    }

    static std::string encodeCP37Hex(StringRef Bytes) {
      std::string Hex;
      const char Digits[] = "0123456789ABCDEF";
      for (uint8_t Byte : Bytes.bytes()) {
        uint8_t Encoded = encodeCP37Byte(Byte);
        Hex.push_back(Digits[Encoded >> 4]);
        Hex.push_back(Digits[Encoded & 0x0F]);
      }
      return Hex;
    }

    void addMapRecord(const GeneratedName &Name, std::string Kind,
                      std::string Original, uint32_t ArenaOffset,
                      uint32_t Size, uint32_t Alignment,
                      std::optional<SourceLocationRecord> SourceLocation,
                      std::string Encoding = "") {
      MapRecords.push_back({Name.Name,
                            std::move(Kind),
                            Name.Class,
                            std::move(Original),
                            Name.Ordinal,
                            Name.Collision ? std::optional<uint32_t>(
                                                 Name.CollisionOrdinal)
                                           : std::nullopt,
                            Name.Collision,
                            NameAllocator::getMaxMINameLength(),
                            Name.Hash,
                            std::move(SourceLocation),
                            ArenaOffset,
                            Size,
                            Alignment,
                            std::move(Encoding)});
    }

    void addGlobalObject(const GlobalVariable &GV, const GeneratedName &Name,
                         std::string Kind, uint32_t Offset, uint32_t Size,
                         uint32_t Alignment, AccessWidth Width,
                         bool DirectScalar) {
      Globals[&GV] = {Name.Name, Kind, Offset, Size, Alignment, Width,
                      DirectScalar};
    }

    std::optional<uint32_t> getConstantPointerOffset(const Constant *C) const {
      if (isa<ConstantPointerNull>(C))
        return 0;

      const Value *Base = C;
      uint64_t Offset = 0;
      if (const auto *GEP = dyn_cast<GEPOperator>(C)) {
        APInt ConstantOffset(DL.getIndexSizeInBits(0), 0);
        if (!GEP->accumulateConstantOffset(DL, ConstantOffset))
          return std::nullopt;
        Base = GEP->getPointerOperand()->stripPointerCasts();
        Offset = ConstantOffset.getZExtValue();
      } else {
        Base = C->stripPointerCasts();
      }

      const auto *GV = dyn_cast<GlobalVariable>(Base);
      if (!GV)
        return std::nullopt;

      auto It = Globals.find(GV);
      if (It == Globals.end() || Offset > It->second.Size)
        return std::nullopt;
      return It->second.Offset + Offset;
    }

    void writeArenaConstantBytes(const Constant *C,
                                 MutableArrayRef<uint8_t> Bytes) const {
      if (isa<UndefValue>(C) || isa<ConstantAggregateZero>(C)) {
        std::fill(Bytes.begin(), Bytes.end(), 0);
        return;
      }

      if (C->getType()->isPointerTy()) {
        std::optional<uint32_t> Offset = getConstantPointerOffset(C);
        if (!Offset)
          fail("arena pointer constants referencing laid-out globals or null");
        storeIntegerBytes(APInt(32, *Offset), Bytes.size(), Bytes);
        return;
      }

      Type *Ty = C->getType();
      if (auto *AT = dyn_cast<ArrayType>(Ty)) {
        uint64_t EltSize = DL.getTypeAllocSize(AT->getElementType());
        for (uint64_t I = 0, E = AT->getNumElements(); I != E; ++I) {
          const Constant *Elt =
              C->getAggregateElement(static_cast<unsigned>(I));
          if (!Elt)
            fail("constant array elements");
          writeArenaConstantBytes(Elt, Bytes.slice(I * EltSize, EltSize));
        }
        return;
      }

      if (auto *ST = dyn_cast<StructType>(Ty)) {
        const StructLayout *Layout = DL.getStructLayout(ST);
        for (unsigned I = 0, E = ST->getNumElements(); I != E; ++I) {
          Type *EltTy = ST->getElementType(I);
          uint64_t EltSize = DL.getTypeAllocSize(EltTy);
          const Constant *Elt = C->getAggregateElement(I);
          if (!Elt)
            fail("constant struct elements");
          writeArenaConstantBytes(
              Elt, Bytes.slice(Layout->getElementOffset(I), EltSize));
        }
        return;
      }

      OS400MIEmitPass::writeConstantBytes(DL, C, Bytes);
    }

    void layoutIntegerGlobal(const GlobalVariable &GV) {
      const auto *CI = dyn_cast<ConstantInt>(GV.getInitializer());
      if (!CI || !(CI->getType()->isIntegerTy(8) ||
                   CI->getType()->isIntegerTy(16) ||
                   CI->getType()->isIntegerTy(32) ||
                   CI->getType()->isIntegerTy(64)))
        fail("i8/i16/i32/i64 globals with constant integer initializers");

      AccessWidth Width = getIntegerAccessWidth(CI->getType());
      uint32_t Size = getAccessWidthBytes(Width);
      uint32_t Alignment = Width == AccessWidth::I8 ? 1 : std::min(Size, 4U);
      uint32_t Offset = alignTo(NextArenaOffset, Alignment);
      if (Offset + Size > ArenaSize)
        fail("phase-1 arena storage within 256 bytes");

      std::string Original = getOriginalName(GV);
      GeneratedName Name = Names.createGlobalName(Original);
      if (Width == AccessWidth::I8) {
        std::string Hex;
        raw_string_ostream HexOS(Hex);
        HexOS << format_hex_no_prefix(CI->getZExtValue() & 0xFF, 2, true);
        Declarations.push_back("DCL     DD          " + Name.Name +
                               "    CHAR(1)    DEF(C_MEM) POS(" +
                               std::to_string(Offset + 1) + ") INIT(X'" + Hex +
                               "');");
      } else if (Width == AccessWidth::I16) {
        Declarations.push_back("DCL     DD          " + Name.Name +
                               "    BIN(2)     UNSGND DEF(C_MEM) POS(" +
                               std::to_string(Offset + 1) + ") INIT(" +
                               std::to_string(CI->getZExtValue() & 0xFFFF) +
                               ");");
      } else if (Width == AccessWidth::I32) {
        int32_t InitialValue = static_cast<int32_t>(CI->getSExtValue());
        Declarations.push_back("DCL     DD          " + Name.Name +
                               "    BIN(4)     DEF(C_MEM) POS(" +
                               std::to_string(Offset + 1) + ") INIT(" +
                               std::to_string(InitialValue) + ");");
      } else {
        SmallVector<uint8_t, 8> Bytes(Size, 0);
        storeIntegerBytes(CI->getValue(), Size, Bytes);
        Declarations.push_back("DCL DD " + Name.Name + " CHAR(8) DEF(C_MEM) POS(" +
                               std::to_string(Offset + 1) + ") INIT(X'" +
                               getRawHex(Bytes) + "');");
      }
      addGlobalObject(GV, Name, "global", Offset, Size, Alignment, Width,
                      Width != AccessWidth::I64);
      addMapRecord(Name, "global", std::move(Original), Offset, Size, Alignment,
                   getSourceLocation(GV));
      NextArenaOffset = Offset + Size;
    }

    void layoutPointerGlobal(const GlobalVariable &GV) {
      if (!GV.getValueType()->isPointerTy())
        fail("PTR32 globals");

      std::optional<uint32_t> PointerOffset =
          getConstantPointerOffset(GV.getInitializer());
      if (!PointerOffset)
        fail("PTR32 globals initialized with null or laid-out globals");

      uint32_t Size = 4;
      uint32_t Alignment = 4;
      uint32_t Offset = alignTo(NextArenaOffset, Alignment);
      if (Offset + Size > ArenaSize)
        fail("phase-1 arena storage within 256 bytes");

      std::string Original = getOriginalName(GV);
      GeneratedName Name = Names.createGlobalName(Original);
      Declarations.push_back("DCL     DD          " + Name.Name +
                             "    BIN(4)     UNSGND DEF(C_MEM) POS(" +
                             std::to_string(Offset + 1) + ") INIT(" +
                             std::to_string(*PointerOffset) + ");");
      addGlobalObject(GV, Name, "ptr_global", Offset, Size, Alignment,
                      AccessWidth::I32, true);
      addMapRecord(Name, "ptr_global", std::move(Original), Offset, Size,
                   Alignment, getSourceLocation(GV), "ptr32");
      NextArenaOffset = Offset + Size;
    }

    void layoutStringGlobal(const GlobalVariable &GV,
                            const ConstantDataArray &CDA) {
      if (!CDA.isString())
        fail("constant i8 string globals");

      StringRef Bytes = CDA.getAsString();
      uint32_t Size = Bytes.size();
      uint32_t Offset = alignTo(NextArenaOffset, 1);
      if (Offset + Size > ArenaSize)
        fail("phase-1 arena storage within 256 bytes");

      std::string Original = getOriginalName(GV);
      GeneratedName Name = Names.createLiteralName(Original);
      std::string Hex = encodeCP37Hex(Bytes);
      Declarations.push_back("DCL     DD          " + Name.Name + "    CHAR(" +
                             std::to_string(Size) +
                             ")    DEF(C_MEM) POS(" +
                             std::to_string(Offset + 1) + ") INIT(X'" + Hex +
                             "');");
      addGlobalObject(GV, Name, "string", Offset, Size, 1, AccessWidth::I8,
                      false);
      addMapRecord(Name, "string", std::move(Original), Offset, Size, 1,
                   getSourceLocation(GV), "ibm-037");
      NextArenaOffset = Offset + Size;
    }

    void layoutAggregateGlobal(const GlobalVariable &GV) {
      Type *ValueTy = GV.getValueType();
      if (!isSupportedArenaAggregateType(ValueTy))
        fail("integer array/struct globals");

      uint32_t Size = static_cast<uint32_t>(DL.getTypeAllocSize(ValueTy));
      uint32_t Alignment = getABIAlignment(DL, ValueTy);
      uint32_t Offset = alignTo(NextArenaOffset, Alignment);
      if (Offset + Size > ArenaSize)
        fail("phase-1 arena storage within 256 bytes");

      SmallVector<uint8_t, 32> Bytes(Size, 0);
      writeArenaConstantBytes(GV.getInitializer(), Bytes);

      std::string Original = getOriginalName(GV);
      GeneratedName Name = Names.createGlobalName(Original);
      Declarations.push_back("DCL DD " + Name.Name + " CHAR(" +
                             std::to_string(Size) + ") DEF(C_MEM) POS(" +
                             std::to_string(Offset + 1) + ") INIT(X'" +
                             getRawHex(Bytes) + "');");
      addGlobalObject(GV, Name, "aggregate", Offset, Size, Alignment,
                      AccessWidth::I8, false);
      addMapRecord(Name, "aggregate", std::move(Original), Offset, Size,
                   Alignment, getSourceLocation(GV), "raw-bytes");
      NextArenaOffset = Offset + Size;
    }

  public:
    explicit ArenaLayout(const DataLayout &DL, NameAllocator &Names)
        : Names(Names), DL(DL) {}

    void lower(const Module &M) {
      for (const GlobalVariable &GV : M.globals()) {
        if (GV.isDeclaration())
          fail("defined globals only");

        Type *ValueTy = GV.getValueType();
        if (ValueTy->isIntegerTy(8) || ValueTy->isIntegerTy(16) ||
            ValueTy->isIntegerTy(32) || ValueTy->isIntegerTy(64)) {
          layoutIntegerGlobal(GV);
          continue;
        }

        if (ValueTy->isPointerTy()) {
          layoutPointerGlobal(GV);
          continue;
        }

        const auto *ArrayTy = dyn_cast<ArrayType>(ValueTy);
        const auto *CDA = dyn_cast<ConstantDataArray>(GV.getInitializer());
        if (ArrayTy && ArrayTy->getElementType()->isIntegerTy(8) && CDA &&
            isCP37StringLiteral(*CDA)) {
          layoutStringGlobal(GV, *CDA);
          continue;
        }

        if (isa<ConstantAggregateZero>(GV.getInitializer()) ||
            isa<ConstantArray>(GV.getInitializer()) ||
            isa<ConstantStruct>(GV.getInitializer()) ||
            isa<ConstantDataArray>(GV.getInitializer())) {
          layoutAggregateGlobal(GV);
          continue;
        }

        fail("i8/i16/i32/i64/PTR32 globals, constant i8 string globals, and "
             "integer/PTR32 array/struct globals");
      }
    }

    void writeConstantBytes(const Constant *C,
                            MutableArrayRef<uint8_t> Bytes) const {
      writeArenaConstantBytes(C, Bytes);
    }

    std::optional<ArenaSlot> getGlobalSlot(const Value *Ptr,
                                           uint64_t Offset = 0) const {
      const auto *GV = dyn_cast<GlobalVariable>(Ptr->stripPointerCasts());
      if (!GV)
        return std::nullopt;

      auto It = Globals.find(GV);
      if (It == Globals.end() || Offset >= It->second.Size)
        return std::nullopt;
      return ArenaSlot{It->second.Name, It->second.Offset, It->second.Size,
                       It->second.Width, It->second.DirectScalar && Offset == 0};
    }

    uint32_t getNextArenaOffset() const { return NextArenaOffset; }

    uint32_t getArenaSize() const {
      return std::max<uint32_t>(ArenaSize, alignTo(NextArenaOffset, 16));
    }

    ArrayRef<std::string> getDeclarations() const { return Declarations; }
    ArrayRef<MapRecord> getMapRecords() const { return MapRecords; }
  };

  struct FunctionInfo {
    struct Slot {
      Type *Ty = nullptr;
      std::string Name;
      I64Value I64;
      uint32_t Size = 0;
      uint32_t Alignment = 0;
    };

    const Function *F = nullptr;
    std::string EntryName;
    std::string ReturnPointerName;
    Slot ReturnSlot;
    SmallVector<Slot, 4> Args;
  };

  class ModuleFunctionPlan {
    NameAllocator &Names;
    SmallVector<const Function *, 8> Functions;
    DenseMap<const Function *, FunctionInfo> Infos;
    DenseMap<const Function *, unsigned> VisitState;
    SmallVector<std::string, 8> Declarations;
    SmallVector<MapRecord, 8> MapRecords;

    static std::string getOriginalName(const Function &F) {
      if (F.hasName())
        return F.getName().str();
      return "";
    }

    static bool isSupportedCallABIType(Type *Ty) {
      return Ty->isIntegerTy(1) || Ty->isIntegerTy(8) ||
             Ty->isIntegerTy(16) || Ty->isIntegerTy(32) ||
             Ty->isIntegerTy(64) || Ty->isPointerTy() ||
             isSupportedArenaValueType(Ty);
    }

    static bool isSupportedCallType(const FunctionType *Ty) {
      if (Ty->isVarArg() || !isSupportedCallABIType(Ty->getReturnType()))
        return false;
      for (Type *ParamTy : Ty->params()) {
        if (!isSupportedCallABIType(ParamTy))
          return false;
      }
      return true;
    }

    static bool isI64Slot(Type *Ty) { return Ty->isIntegerTy(64); }
    static bool isNarrowIntSlot(Type *Ty) {
      return Ty->isIntegerTy(1) || Ty->isIntegerTy(8) || Ty->isIntegerTy(16);
    }
    static bool isAggregateSlot(Type *Ty) {
      return !(Ty->isIntegerTy(1) || Ty->isIntegerTy(8) ||
               Ty->isIntegerTy(16) || Ty->isIntegerTy(32) ||
               Ty->isIntegerTy(64) || Ty->isPointerTy());
    }

    static void declareSlot(SmallVectorImpl<std::string> &Declarations,
                            const FunctionInfo::Slot &Slot) {
      if (Slot.Ty->isIntegerTy(64)) {
        appendI64Declarations(Declarations, Slot.I64);
        return;
      }

      if (isAggregateSlot(Slot.Ty)) {
        Declarations.push_back("DCL     DD          " + Slot.Name +
                               "    CHAR(" + std::to_string(Slot.Size) +
                               ");");
        return;
      }

      std::string Decl = "DCL     DD          " + Slot.Name;
      if (Slot.Ty->isPointerTy())
        Decl += "    BIN(4)     UNSGND;";
      else
        Decl += "    BIN(4);";
      Declarations.push_back(std::move(Decl));
      if (isNarrowIntSlot(Slot.Ty))
        Declarations.push_back("DCL     DD          " + Slot.Name +
                               "B   CHAR(4)    DEF(" + Slot.Name +
                               ") POS(1);");
    }

    static FunctionInfo::Slot makeSlot(const DataLayout &DL, Type *Ty,
                                       StringRef Name) {
      FunctionInfo::Slot Slot;
      Slot.Ty = Ty;
      Slot.Name = Name.str();
      if (isI64Slot(Ty))
        Slot.I64 = makeI64Value(Name);
      if (isAggregateSlot(Ty)) {
        uint64_t Size = DL.getTypeAllocSize(Ty);
        if (Size > std::numeric_limits<uint32_t>::max())
          fail("aggregate call slots no larger than 4 GiB");
        Slot.Size = static_cast<uint32_t>(Size);
        Slot.Alignment = getABIAlignment(DL, Ty);
      }
      return Slot;
    }

    void addMapRecord(const GeneratedName &Name, const Function &F,
                      StringRef Kind) {
      MapRecords.push_back({Name.Name,
                            Kind.str(),
                            Name.Class,
                            getOriginalName(F),
                            Name.Ordinal,
                            Name.Collision ? std::optional<uint32_t>(
                                                 Name.CollisionOrdinal)
                                           : std::nullopt,
                            Name.Collision,
                            NameAllocator::getMaxMINameLength(),
                            Name.Hash,
                            getSourceLocation(F),
                            std::nullopt,
                            std::nullopt,
                            std::nullopt,
                            ""});
    }

    void createMainInfo(const Function &Main) {
      FunctionInfo Info;
      Info.F = &Main;
      Info.EntryName = "MAIN";
      Info.ReturnPointerName = ".MAIN";
      Info.ReturnSlot =
          makeSlot(Main.getParent()->getDataLayout(), Main.getReturnType(),
                   "MAIN_RC");
      Infos[&Main] = std::move(Info);
      MapRecords.push_back({"MAIN",
                            "function",
                            "function",
                            "main",
                            std::nullopt,
                            std::nullopt,
                            false,
                            NameAllocator::getMaxMINameLength(),
                            "",
                            getSourceLocation(Main),
                            std::nullopt,
                            std::nullopt,
                            std::nullopt,
                            ""});
    }

    void createHelperInfo(const Function &F) {
      const DataLayout &DL = F.getParent()->getDataLayout();
      GeneratedName Entry = Names.createFunctionName(getOriginalName(F));
      FunctionInfo Info;
      Info.F = &F;
      Info.EntryName = Entry.Name;
      Info.ReturnPointerName = "." + Entry.Name;
      Info.ReturnSlot = makeSlot(DL, F.getReturnType(), Entry.Name + "R");
      for (unsigned I = 0, E = F.arg_size(); I != E; ++I)
        Info.Args.push_back(
            makeSlot(DL, F.getArg(I)->getType(),
                     formatv("{0}A{1}", Entry.Name, I + 1).str()));

      Declarations.push_back("DCL     INSPTR      " + Info.ReturnPointerName +
                             ";");
      declareSlot(Declarations, Info.ReturnSlot);
      for (const FunctionInfo::Slot &Arg : Info.Args)
        declareSlot(Declarations, Arg);

      addMapRecord(Entry, F, "function");
      Infos[&F] = std::move(Info);
    }

    void ensureInfo(const Function &F) {
      if (Infos.contains(&F))
        return;
      createHelperInfo(F);
    }

    void validateFunctionSignature(const Function &F, bool IsMain) const {
      FunctionType *Ty = F.getFunctionType();
      if (IsMain) {
        if (!Ty->getReturnType()->isIntegerTy(32) || Ty->getNumParams() != 0)
          fail("int main(void)");
        return;
      }

      if (!isSupportedCallType(Ty))
        fail("non-varargs direct calls between functions using "
             "i1/i8/i16/i32/i64/PTR32 arguments and returns");
    }

    void visitFunction(const Function &F, bool IsMain) {
      if (F.isDeclaration())
        fail("defined functions for direct internal calls");
      if (F.empty())
        fail("non-empty defined functions");

      unsigned &State = VisitState[&F];
      if (State == 1)
        fail("acyclic direct calls; recursion requires a software stack");
      if (State == 2)
        return;

      State = 1;
      validateFunctionSignature(F, IsMain);
      ensureInfo(F);
      Functions.push_back(&F);

      for (const BasicBlock &BB : F) {
        for (const Instruction &I : BB) {
          const auto *CB = dyn_cast<CallBase>(&I);
          if (!CB)
            continue;

          if (CB->isInlineAsm()) {
            validateRawInlineAsm(*CB);
            continue;
          }

          if (CB->isIndirectCall())
            fail("direct function calls; indirect calls require a function "
                 "pointer ABI");

          const Function *Callee = CB->getCalledFunction();
          if (!Callee)
            fail("direct function calls");
          if (getAggregateComparePseudoKind(Callee))
            continue;
          if (isa<IntrinsicInst>(I))
            continue;
          if (Callee->isDeclaration())
            fail("defined internal callees; external calls require CALLX ABI");

          validateFunctionSignature(*Callee, false);
          visitFunction(*Callee, false);
        }
      }

      State = 2;
    }

  public:
    explicit ModuleFunctionPlan(NameAllocator &Names) : Names(Names) {}

    void analyze(const Module &M) {
      const Function *Main = M.getFunction("main");
      if (!Main || Main->isDeclaration())
        fail("a defined i32 main function");

      createMainInfo(*Main);
      visitFunction(*Main, true);

      for (const Function &F : M.functions()) {
        if (!F.isDeclaration() && !VisitState.contains(&F))
          fail("defined functions reachable from main");
      }
    }

    const Function &getMainFunction() const {
      if (Functions.empty())
        fail("a defined i32 main function");
      return *Functions.front();
    }

    ArrayRef<const Function *> getFunctions() const { return Functions; }

    const FunctionInfo &getInfo(const Function &F) const {
      auto It = Infos.find(&F);
      if (It == Infos.end())
        fail("function present in module plan");
      return It->second;
    }

    ArrayRef<std::string> getDeclarations() const { return Declarations; }
    ArrayRef<MapRecord> getMapRecords() const { return MapRecords; }
  };

  class FunctionLowerer {
    NameAllocator &Names;
    const DataLayout &DL;
    const ArenaLayout &Layout;
    const ModuleFunctionPlan &FunctionPlan;
    const FunctionInfo *CurrentFunction = nullptr;
    DenseMap<const Value *, std::string> Values;
    DenseMap<const Value *, I64Value> I64Values;
    DenseMap<const Value *, ArenaSlot> AggregateValues;
    DenseMap<const Value *, std::string> SignedNarrowValues;
    DenseMap<const Value *, CompareValue> Comparisons;
    DenseMap<const Value *, I64CompareValue> I64Comparisons;
    DenseMap<const AllocaInst *, ArenaSlot> Slots;
    DenseMap<const BasicBlock *, std::string> BlockLabels;
    DenseMap<const BasicBlock *, SmallVector<const PHINode *, 2>> BlockPHIs;
    SmallVector<std::string, 8> Declarations;
    SmallVector<std::string, 16> Body;
    SmallVector<std::string, 16> EdgeBlocks;
    SmallVector<MapRecord, 16> MapRecords;
    uint32_t NextArenaOffset;
    uint32_t NextCallBarrier = 1;
    bool HasLoadStoreLens = false;
    bool HasU1Box = false;

    static bool isI32(Type *Ty) { return Ty && Ty->isIntegerTy(32); }
    static bool isPTR32(Type *Ty) { return Ty && Ty->isPointerTy(); }
    static bool isI32Like(Type *Ty) { return isI32(Ty) || isPTR32(Ty); }
    static bool isSupportedInt(Type *Ty) {
      return Ty && (Ty->isIntegerTy(8) || Ty->isIntegerTy(16) ||
                    Ty->isIntegerTy(32) || Ty->isIntegerTy(64));
    }

    static bool isSupportedCompareInt(Type *Ty) {
      return Ty && (Ty->isIntegerTy(8) || Ty->isIntegerTy(16) ||
                    Ty->isIntegerTy(32) || Ty->isIntegerTy(64) ||
                    Ty->isPointerTy());
    }

    static bool isAggregateABIType(Type *Ty) {
      return isAggregateABIValueType(Ty);
    }

    static uint32_t alignTo4(uint32_t Value) { return (Value + 3) & ~3U; }
    static uint32_t alignTo(uint32_t Value, uint32_t Alignment) {
      return (Value + Alignment - 1) & ~(Alignment - 1);
    }

    static std::string getOriginalName(const Value &V) {
      if (V.hasName())
        return V.getName().str();
      return "";
    }

    void addMapRecord(const GeneratedName &Name, std::string Kind,
                      std::string Original = "",
                      std::optional<SourceLocationRecord> SourceLocation =
                          std::nullopt,
                      std::optional<uint32_t> ArenaOffset = std::nullopt,
                      std::optional<uint32_t> Size = std::nullopt,
                      std::optional<uint32_t> Alignment = std::nullopt) {
      MapRecords.push_back({Name.Name,
                            std::move(Kind),
                            Name.Class,
                            std::move(Original),
                            Name.Ordinal,
                            Name.Collision ? std::optional<uint32_t>(
                                                 Name.CollisionOrdinal)
                                           : std::nullopt,
                            Name.Collision,
                            NameAllocator::getMaxMINameLength(),
                            Name.Hash,
                            std::move(SourceLocation),
                            ArenaOffset,
                            Size,
                            Alignment,
                            ""});
    }

    void addFrameMapRecord(uint32_t FrameOffset, uint32_t FrameSize) {
      std::string Name = CurrentFunction->EntryName + "_FRAME";
      if (Name.size() > NameAllocator::getMaxMINameLength())
        fail("function frame map names no longer than 48 characters");
      MapRecords.push_back({std::move(Name),
                            "frame",
                            "frame",
                            CurrentFunction->F ? getOriginalName(
                                                     *CurrentFunction->F)
                                               : "",
                            std::nullopt,
                            std::nullopt,
                            false,
                            NameAllocator::getMaxMINameLength(),
                            "",
                            CurrentFunction->F
                                ? getSourceLocation(*CurrentFunction->F)
                                : std::nullopt,
                            FrameOffset,
                            FrameSize,
                            4,
                            "static-arena;activation=per-function;"
                            "reentrant=false;recursion=unsupported;"
                            "future=software-stack"});
    }

    void addCallAliasBarrierRecord(const CallBase &CB,
                                   const FunctionInfo &CalleeInfo) {
      std::string Name =
          CurrentFunction->EntryName + "_CALLB" + std::to_string(NextCallBarrier++);
      if (Name.size() > NameAllocator::getMaxMINameLength())
        fail("call alias barrier map names no longer than 48 characters");
      MapRecords.push_back({std::move(Name),
                            "call_alias_barrier",
                            "call_barrier",
                            CalleeInfo.F ? getOriginalName(*CalleeInfo.F) : "",
                            std::nullopt,
                            std::nullopt,
                            false,
                            NameAllocator::getMaxMINameLength(),
                            "",
                            getSourceLocation(cast<Instruction>(CB)),
                            std::nullopt,
                            std::nullopt,
                            std::nullopt,
                            "arena-memory;invalidate=promoted-scalars;"
                            "scope=conservative"});
    }

    void invalidateCallAliasedMemory(const CallBase &CB,
                                     const FunctionInfo &CalleeInfo) {
      // Loads are always emitted as loads today, so there are no live promoted
      // memory values to discard yet. Keep this as the single hook future
      // promotion caches must clear when a callee may touch arena memory.
      addCallAliasBarrierRecord(CB, CalleeInfo);
    }

    std::string getOperandName(const Value *V) const {
      if (V->getType()->isPointerTy()) {
        auto It = Values.find(V);
        if (It != Values.end())
          return It->second;
        if (std::optional<std::string> Offset = tryGetPointerOffsetName(V))
          return *Offset;
        fail("PTR32 operands defined by arena pointers or previous loads");
      }

      if (const auto *CI = dyn_cast<ConstantInt>(V)) {
        if (!CI->getType()->isIntegerTy(1) &&
            !(CI->getType()->isIntegerTy(8) || CI->getType()->isIntegerTy(16) ||
              CI->getType()->isIntegerTy(32)))
          fail("i1/i8/i16/i32 constants in lowered expressions");
        if (CI->getType()->isIntegerTy(1))
          return CI->isOne() ? "1" : "0";
        return std::to_string(static_cast<int32_t>(CI->getSExtValue()));
      }

      auto It = Values.find(V);
      if (It == Values.end())
        fail("operands defined by previous supported i32 instructions");
      return It->second;
    }

    I64Value getI64Operand(const Value *V) {
      if (!V->getType()->isIntegerTy(64))
        fail("i64 operands");

      if (const auto *CI = dyn_cast<ConstantInt>(V))
        return materializeI64Constant(*CI);

      auto It = I64Values.find(V);
      if (It == I64Values.end())
        fail("operands defined by previous supported i64 instructions");
      return It->second;
    }

    std::string getCompareOperandName(const Value *V, bool Signed = false) {
      if (const auto *CI = dyn_cast<ConstantInt>(V)) {
        Type *Ty = CI->getType();
        if (Signed && (Ty->isIntegerTy(8) || Ty->isIntegerTy(16)))
          return std::to_string(static_cast<int32_t>(CI->getSExtValue()));
        if (Ty->isIntegerTy(8))
          return std::to_string(CI->getZExtValue() & 0xFF);
        if (Ty->isIntegerTy(16))
          return std::to_string(CI->getZExtValue() & 0xFFFF);
      }
      if (Signed && (V->getType()->isIntegerTy(8) ||
                     V->getType()->isIntegerTy(16)))
        return materializeSignedNarrowOperand(V);
      return getOperandName(V);
    }

    std::string materializeSignedNarrowOperand(const Value *V) {
      auto Existing = SignedNarrowValues.find(V);
      if (Existing != SignedNarrowValues.end())
        return Existing->second;

      Type *Ty = V->getType();
      if (!Ty->isIntegerTy(8) && !Ty->isIntegerTy(16))
        fail("signed i8/i16 compare operands");

      std::string Src = getOperandName(V);
      std::string Dest = createAnonymousTemp("signed_narrow");
      GeneratedName DoneName = Names.createBlockName();
      std::string DoneLabel = DoneName.Name;
      addMapRecord(DoneName, "signed_narrow_done", "", std::nullopt);

      uint32_t SignLimit = Ty->isIntegerTy(8) ? 127 : 32767;
      uint32_t Modulus = Ty->isIntegerTy(8) ? 256 : 65536;
      Body.push_back("        CPYNV       " + Dest + "," + Src + ";");
      Body.push_back("        CMPNV(B)    " + Src + "," +
                     std::to_string(SignLimit) + "/NHI(" + DoneLabel + ");");
      Body.push_back("        SUBN        " + Dest + "," + Src + "," +
                     std::to_string(Modulus) + ";");
      Body.push_back(DoneLabel + ":");
      SignedNarrowValues[V] = Dest;
      return Dest;
    }

    std::string getBlockLabel(const BasicBlock *BB) const {
      auto It = BlockLabels.find(BB);
      if (It == BlockLabels.end())
        fail("branches to blocks in main");
      return It->second;
    }

    void ensureLoadStoreLens() {
      if (HasLoadStoreLens)
        return;

      HasLoadStoreLens = true;
      Declarations.push_back("DCL     SPCPTR      .LS;");
      Declarations.push_back("DCL     DD          OFF       BIN(4);");
      Declarations.push_back("DCL     SPC         LOADSTORE BAS(.LS);");
      Declarations.push_back("DCL     DD          LS_I1     CHAR(1)    DIR        POS(1);");
      Declarations.push_back("DCL     DD          LS_I2     BIN(2)     UNSGND DIR POS(1);");
      Declarations.push_back("DCL     DD          LS_I4     BIN(4)     DIR        POS(1);");
    }

    void ensureU1Box() {
      if (HasU1Box)
        return;

      HasU1Box = true;
      Declarations.push_back("DCL     DD          U1_BOX    CHAR(4);");
      Declarations.push_back("DCL     DD          U1_NUM    BIN(4)     DEF(U1_BOX) POS(1);");
      Declarations.push_back("DCL     DD          U1_BYTE   CHAR(1)    DEF(U1_BOX) POS(4);");
    }

    static StringRef getBranchPredicate(CmpInst::Predicate Predicate) {
      switch (Predicate) {
      case CmpInst::ICMP_EQ:
        return "EQ";
      case CmpInst::ICMP_NE:
        return "NEQ";
      case CmpInst::ICMP_SLT:
        return "LO";
      case CmpInst::ICMP_SGT:
        return "HI";
      case CmpInst::ICMP_SLE:
      case CmpInst::ICMP_ULE:
        return "NHI";
      case CmpInst::ICMP_SGE:
      case CmpInst::ICMP_UGE:
        return "NLO";
      case CmpInst::ICMP_ULT:
        return "LO";
      case CmpInst::ICMP_UGT:
        return "HI";
      default:
        fail("i32 icmp predicates");
        llvm_unreachable("fail should not return");
      }
    }

    std::optional<ArenaSlot> getConstantGlobalSlot(const Value *V) const {
      const Value *Base = V;
      uint64_t Offset = 0;
      if (const auto *GEP = dyn_cast<GEPOperator>(V)) {
        APInt ConstantOffset(DL.getIndexSizeInBits(0), 0);
        if (!GEP->accumulateConstantOffset(DL, ConstantOffset))
          return std::nullopt;
        Offset = ConstantOffset.getZExtValue();
        Base = GEP->getPointerOperand();
      }
      return Layout.getGlobalSlot(Base, Offset);
    }

    std::optional<uint32_t> getBaseArenaOffset(const Value *V) const {
      if (const auto *AI = dyn_cast<AllocaInst>(V)) {
        auto It = Slots.find(AI);
        if (It == Slots.end())
          return std::nullopt;
        return It->second.Offset;
      }

      if (std::optional<ArenaSlot> Slot = Layout.getGlobalSlot(V))
        return Slot->Offset;

      return std::nullopt;
    }

    std::optional<ArenaSlot> getDirectScalarSlot(const Value *V) const {
      if (std::optional<ArenaSlot> Slot = getConstantGlobalSlot(V)) {
        if (Slot->DirectScalar)
          return Slot;
        return std::nullopt;
      }

      const auto *AI = dyn_cast<AllocaInst>(V);
      if (!AI)
        return std::nullopt;

      auto It = Slots.find(AI);
      if (It == Slots.end())
        return std::nullopt;
      if (!It->second.DirectScalar)
        return std::nullopt;
      return It->second;
    }

    std::string createTemp(const Value &V) {
      std::string Original = getOriginalName(V);
      GeneratedName Name = Names.createTempName(Original);
      Declarations.push_back("DCL     DD          " + Name.Name +
                             "    BIN(4);");
      Declarations.push_back("DCL     DD          " + Name.Name +
                             "B   CHAR(4)    DEF(" + Name.Name + ") POS(1);");
      addMapRecord(Name, "temp", std::move(Original),
                   isa<Instruction>(&V)
                       ? getSourceLocation(cast<Instruction>(V))
                       : std::nullopt,
                   std::nullopt, 4, 4);
      return Name.Name;
    }

    std::string createAnonymousTemp(StringRef Kind) {
      GeneratedName Name = Names.createTempName();
      Declarations.push_back("DCL     DD          " + Name.Name +
                             "    BIN(4);");
      Declarations.push_back("DCL     DD          " + Name.Name +
                             "B   CHAR(4)    DEF(" + Name.Name + ") POS(1);");
      addMapRecord(Name, Kind.str(), "", std::nullopt, std::nullopt, 4, 4);
      return Name.Name;
    }

    I64Value createI64Temp(const Value &V, StringRef Kind = "i64_temp") {
      std::string Original = getOriginalName(V);
      GeneratedName Name = Names.createTempName(Original);
      I64Value Temp;
      Temp.Bytes = Name.Name;
      Temp.Hi = Name.Name + "H";
      Temp.HiU = Name.Name + "U";
      Temp.Lo = Name.Name + "L";
      Temp.LoBytes = Name.Name + "LB";
      for (unsigned I = 0; I != 8; ++I)
        Temp.Byte[I] = formatv("{0}B{1}", Name.Name, I + 1).str();

      Declarations.push_back("DCL     DD          " + Temp.Bytes +
                             "    CHAR(8);");
      Declarations.push_back("DCL     DD          " + Temp.Hi +
                             "    BIN(4)     DEF(" + Temp.Bytes + ") POS(1);");
      Declarations.push_back("DCL     DD          " + Temp.HiU +
                             "    BIN(4)     UNSGND DEF(" + Temp.Bytes +
                             ") POS(1);");
      Declarations.push_back("DCL     DD          " + Temp.Lo +
                             "    BIN(4)     UNSGND DEF(" + Temp.Bytes +
                             ") POS(5);");
      Declarations.push_back("DCL     DD          " + Temp.LoBytes +
                             "   CHAR(4)    DEF(" + Temp.Bytes + ") POS(5);");
      for (unsigned I = 0; I != 8; ++I)
        Declarations.push_back("DCL     DD          " + Temp.Byte[I] +
                               "   CHAR(1)    DEF(" + Temp.Bytes + ") POS(" +
                               std::to_string(I + 1) + ");");

      addMapRecord(Name, Kind.str(), std::move(Original),
                   isa<Instruction>(&V)
                       ? getSourceLocation(cast<Instruction>(V))
                       : std::nullopt,
                   std::nullopt, 8, 4);
      return Temp;
    }

    I64Value createAnonymousI64Temp(StringRef Kind) {
      GeneratedName Name = Names.createTempName();
      I64Value Temp;
      Temp.Bytes = Name.Name;
      Temp.Hi = Name.Name + "H";
      Temp.HiU = Name.Name + "U";
      Temp.Lo = Name.Name + "L";
      Temp.LoBytes = Name.Name + "LB";
      for (unsigned I = 0; I != 8; ++I)
        Temp.Byte[I] = formatv("{0}B{1}", Name.Name, I + 1).str();

      Declarations.push_back("DCL     DD          " + Temp.Bytes +
                             "    CHAR(8);");
      Declarations.push_back("DCL     DD          " + Temp.Hi +
                             "    BIN(4)     DEF(" + Temp.Bytes + ") POS(1);");
      Declarations.push_back("DCL     DD          " + Temp.HiU +
                             "    BIN(4)     UNSGND DEF(" + Temp.Bytes +
                             ") POS(1);");
      Declarations.push_back("DCL     DD          " + Temp.Lo +
                             "    BIN(4)     UNSGND DEF(" + Temp.Bytes +
                             ") POS(5);");
      Declarations.push_back("DCL     DD          " + Temp.LoBytes +
                             "   CHAR(4)    DEF(" + Temp.Bytes + ") POS(5);");
      for (unsigned I = 0; I != 8; ++I)
        Declarations.push_back("DCL     DD          " + Temp.Byte[I] +
                               "   CHAR(1)    DEF(" + Temp.Bytes + ") POS(" +
                               std::to_string(I + 1) + ");");

      addMapRecord(Name, Kind.str(), "", std::nullopt, std::nullopt, 8, 4);
      return Temp;
    }

    I64Value materializeI64Constant(const ConstantInt &CI,
                                    StringRef Kind = "i64_const") {
      I64Value Temp = createAnonymousI64Temp(Kind);
      SmallVector<uint8_t, 8> Bytes(8, 0);
      storeIntegerBytes(CI.getValue(), 8, Bytes);
      Body.push_back("        CPYBLA      " + Temp.Bytes + ",X'" +
                     getRawHex(Bytes) + "';");
      return Temp;
    }

    ArenaSlot createAggregateTemp(const Value &V, Type *Ty, StringRef Kind) {
      if (!isSupportedArenaValueType(Ty))
        fail("integer array/struct aggregate values");

      uint32_t Size = static_cast<uint32_t>(DL.getTypeAllocSize(Ty));
      uint32_t Alignment = getABIAlignment(DL, Ty);
      uint32_t Offset = alignTo(NextArenaOffset, Alignment);
      if (Offset + Size > ArenaSize)
        fail("phase-1 arena storage within 256 bytes");

      std::string Original = getOriginalName(V);
      GeneratedName SlotName = Names.createSlotName(Original);
      ArenaSlot Slot{SlotName.Name, Offset, Size, AccessWidth::I8, false};
      NextArenaOffset = Offset + Size;
      Declarations.push_back("DCL     DD          " + Slot.Name +
                             "    CHAR(" + std::to_string(Size) +
                             ")    DEF(C_MEM) POS(" +
                             std::to_string(Slot.Offset + 1) + ");");
      addMapRecord(SlotName, Kind.str(), std::move(Original),
                   isa<Instruction>(&V)
                       ? getSourceLocation(cast<Instruction>(V))
                       : std::nullopt,
                   Slot.Offset, Size, Alignment);
      return Slot;
    }

    uint64_t getIndexedOffset(Type *BaseTy, ArrayRef<unsigned> Indices) const {
      Type *Ty = BaseTy;
      uint64_t Offset = 0;
      for (unsigned Index : Indices) {
        if (auto *ST = dyn_cast<StructType>(Ty)) {
          if (Index >= ST->getNumElements())
            fail("extractvalue/insertvalue struct indices in range");
          Offset += DL.getStructLayout(ST)->getElementOffset(Index);
          Ty = ST->getElementType(Index);
          continue;
        }

        if (auto *AT = dyn_cast<ArrayType>(Ty)) {
          if (Index >= AT->getNumElements())
            fail("extractvalue/insertvalue array indices in range");
          Ty = AT->getElementType();
          Offset += Index * DL.getTypeAllocSize(Ty);
          continue;
        }

        fail("extractvalue/insertvalue through integer arrays/structs");
      }
      return Offset;
    }

    std::string getI32ByteOperandName(const Value *V) {
      if (!isI32(V->getType()))
        fail("i32 bitwise operands");

      if (const auto *CI = dyn_cast<ConstantInt>(V)) {
        std::string Temp = createAnonymousTemp("bitwise_const");
        Body.push_back("        CPYNV       " + Temp + "," +
                       std::to_string(static_cast<int32_t>(
                           CI->getSExtValue())) +
                       ";");
        return Temp + "B";
      }

      auto It = Values.find(V);
      if (It == Values.end())
        fail("operands defined by previous supported i32 instructions");
      return It->second + "B";
    }

    void maskIntegerToWidth(StringRef Dest, unsigned Bits) {
      if (Bits >= 32)
        return;

      uint32_t MaskValue = Bits == 1 ? 1 : Bits == 8 ? 0xFF : 0xFFFF;
      std::string Mask = createAnonymousTemp("narrow_mask");
      Body.push_back("        CPYNV       " + Mask + "," +
                     std::to_string(MaskValue) + ";");
      Body.push_back("        AND         " + Dest.str() + "B," + Dest.str() +
                     "B," + Mask + "B;");
    }

    void maskIntegerSlotToType(StringRef Slot, Type *Ty) {
      if (!Ty->isIntegerTy(1) && !Ty->isIntegerTy(8) &&
          !Ty->isIntegerTy(16))
        return;
      maskIntegerToWidth(Slot, cast<IntegerType>(Ty)->getBitWidth());
    }

    void lowerNarrowBinaryOperator(const BinaryOperator &BO) {
      Type *Ty = BO.getType();
      unsigned Bits = cast<IntegerType>(Ty)->getBitWidth();
      if (Bits != 8 && Bits != 16)
        fail("i8/i16 arithmetic, bitwise, and shift expressions");

      std::string Dest = createTemp(BO);
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
      case Instruction::Mul:
        Body.push_back("        MULT        " + Dest + "," + LHS + "," + RHS +
                       ";");
        break;
      case Instruction::And:
        Body.push_back("        AND         " + Dest + "B," +
                       getOperandAsI32Bytes(BO.getOperand(0)) + "," +
                       getOperandAsI32Bytes(BO.getOperand(1)) + ";");
        break;
      case Instruction::Or:
        Body.push_back("        OR          " + Dest + "B," +
                       getOperandAsI32Bytes(BO.getOperand(0)) + "," +
                       getOperandAsI32Bytes(BO.getOperand(1)) + ";");
        break;
      case Instruction::Xor:
        Body.push_back("        XOR         " + Dest + "B," +
                       getOperandAsI32Bytes(BO.getOperand(0)) + "," +
                       getOperandAsI32Bytes(BO.getOperand(1)) + ";");
        break;
      case Instruction::Shl:
        Body.push_back("        CPYBTLLS    " + Dest + "," + LHS + "," +
                       getConstantShiftAmount(BO.getOperand(1), Bits) + ";");
        break;
      case Instruction::LShr:
        Body.push_back("        CPYBTRLS    " + Dest + "," + LHS + "," +
                       getConstantShiftAmount(BO.getOperand(1), Bits) + ";");
        break;
      default:
        fail("i8/i16 add/sub/mul/bitwise/constant shift expressions");
      }

      maskIntegerToWidth(Dest, Bits);
      Values[&BO] = Dest;
    }

    void emitI32ShiftLeftOne(StringRef Value) {
      std::string Scratch = createAnonymousTemp("i32_shift_scratch");
      Body.push_back("        CPYBTLLS    " + Scratch + "," + Value.str() +
                     ",1;");
      Body.push_back("        CPYNV       " + Value.str() + "," + Scratch +
                     ";");
    }

    void emitI32LogicalShiftRightOne(StringRef Value) {
      std::string Scratch = createAnonymousTemp("i32_shift_scratch");
      Body.push_back("        CPYBTRLS    " + Scratch + "," + Value.str() +
                     ",1;");
      Body.push_back("        CPYNV       " + Value.str() + "," + Scratch +
                     ";");
    }

    void emitI32ArithmeticShiftRightOne(StringRef Value) {
      std::string Scratch = createAnonymousTemp("i32_shift_scratch");
      Body.push_back("        CPYBTRAS    " + Scratch + "," + Value.str() +
                     ",1;");
      Body.push_back("        CPYNV       " + Value.str() + "," + Scratch +
                     ";");
    }

    void emitI32VariableShift(StringRef Dest, StringRef LHS, StringRef RHS,
                              Instruction::BinaryOps Op) {
      std::string Count = createAnonymousTemp("i32_shift_count");
      GeneratedName LoopName = Names.createBlockName();
      GeneratedName DoneName = Names.createBlockName();
      std::string Loop = LoopName.Name;
      std::string Done = DoneName.Name;
      addMapRecord(LoopName, "i32_shift_loop", "", std::nullopt);
      addMapRecord(DoneName, "i32_shift_done", "", std::nullopt);

      Body.push_back("        CPYNV       " + Dest.str() + "," + LHS.str() +
                     ";");
      Body.push_back("        CPYNV       " + Count + "," + RHS.str() + ";");
      Body.push_back(Loop + ":");
      Body.push_back("        CMPNV(B)    " + Count + ",0/EQ(" + Done + ");");
      if (Op == Instruction::Shl)
        emitI32ShiftLeftOne(Dest);
      else if (Op == Instruction::LShr)
        emitI32LogicalShiftRightOne(Dest);
      else if (Op == Instruction::AShr)
        emitI32ArithmeticShiftRightOne(Dest);
      else
        fail("i32 shift operation");
      Body.push_back("        SUBN        " + Count + "," + Count + ",1;");
      Body.push_back("        B           " + Loop + ";");
      Body.push_back(Done + ":");
    }

    std::string getOperandAsI32Bytes(const Value *V) {
      if (isI32(V->getType()))
        return getI32ByteOperandName(V);

      if (V->getType()->isIntegerTy(8) || V->getType()->isIntegerTy(16)) {
        std::string Temp = createAnonymousTemp("narrow_bitwise_operand");
        Body.push_back("        CPYNV       " + Temp + "," +
                       getOperandName(V) + ";");
        maskIntegerToWidth(Temp,
                           cast<IntegerType>(V->getType())->getBitWidth());
        return Temp + "B";
      }

      fail("i8/i16/i32 bitwise operands");
      llvm_unreachable("fail should not return");
    }

    static std::string getConstantShiftAmount(const Value *V) {
      const auto *CI = dyn_cast<ConstantInt>(V);
      if (!CI)
        fail("constant i32 shift amounts");
      uint64_t Amount = CI->getZExtValue();
      if (Amount > 31)
        fail("i32 shift amounts in range 0..31");
      return std::to_string(Amount);
    }

    static std::string getConstantShiftAmount(const Value *V, unsigned Bits) {
      const auto *CI = dyn_cast<ConstantInt>(V);
      if (!CI)
        fail("constant shift amounts");
      uint64_t Amount = CI->getZExtValue();
      if (Amount >= Bits)
        fail("shift amounts in range");
      return std::to_string(Amount);
    }

    void emitI64Add(const I64Value &Dest, const I64Value &LHS,
                    const I64Value &RHS) {
      I64Value Addend = createAnonymousI64Temp("i64_add_addend");
      I64Value Sum = createAnonymousI64Temp("i64_add_sum");
      I64Value Carry = createAnonymousI64Temp("i64_add_carry");
      GeneratedName LoopName = Names.createBlockName();
      GeneratedName NonZeroName = Names.createBlockName();
      GeneratedName DoneName = Names.createBlockName();
      std::string Loop = LoopName.Name;
      std::string NonZero = NonZeroName.Name;
      std::string Done = DoneName.Name;
      addMapRecord(LoopName, "i64_add_loop", "", std::nullopt);
      addMapRecord(NonZeroName, "i64_add_nonzero", "", std::nullopt);
      addMapRecord(DoneName, "i64_add_done", "", std::nullopt);

      emitI64Copy(Dest, LHS);
      emitI64Copy(Addend, RHS);
      Body.push_back(Loop + ":");
      Body.push_back("        CMPNV(B)    " + Addend.Hi + ",0/NEQ(" +
                     NonZero + ");");
      Body.push_back("        CMPNV(B)    " + Addend.Lo + ",0/EQ(" + Done +
                     ");");
      Body.push_back(NonZero + ":");
      emitI64Bitwise(Sum, Dest, Addend, Instruction::Xor);
      emitI64Bitwise(Carry, Dest, Addend, Instruction::And);
      emitI64ShiftLeftOne(Carry);
      emitI64Copy(Dest, Sum);
      emitI64Copy(Addend, Carry);
      Body.push_back("        B           " + Loop + ";");
      Body.push_back(Done + ":");
    }

    void emitI64Sub(const I64Value &Dest, const I64Value &LHS,
                    const I64Value &RHS) {
      I64Value Ones = createAnonymousI64Temp("i64_ones");
      I64Value One = createAnonymousI64Temp("i64_one");
      I64Value NotRHS = createAnonymousI64Temp("i64_not_rhs");
      I64Value NegRHS = createAnonymousI64Temp("i64_neg_rhs");
      Body.push_back("        CPYBLA      " + Ones.Bytes +
                     ",X'FFFFFFFFFFFFFFFF';");
      Body.push_back("        CPYBLA      " + One.Bytes +
                     ",X'0000000000000001';");
      emitI64Bitwise(NotRHS, RHS, Ones, Instruction::Xor);
      emitI64Add(NegRHS, NotRHS, One);
      emitI64Add(Dest, LHS, NegRHS);
    }

    void emitI64Copy(const I64Value &Dest, const I64Value &Source) {
      Body.push_back("        CPYBLA      " + Dest.Bytes + "," + Source.Bytes +
                     ";");
    }

    void emitI64Zero(const I64Value &Dest) {
      Body.push_back("        CPYBLA      " + Dest.Bytes +
                     ",X'0000000000000000';");
    }

    void emitI64Negate(const I64Value &Dest, const I64Value &Source) {
      I64Value Zero = createAnonymousI64Temp("i64_zero");
      emitI64Zero(Zero);
      emitI64Sub(Dest, Zero, Source);
    }

    void emitI64ShiftLeftOne(const I64Value &Value) {
      I64Value Scratch = createAnonymousI64Temp("i64_shift_scratch");
      Body.push_back("        CPYBTLLS    " + Scratch.Bytes + "," +
                     Value.Bytes + ",1;");
      emitI64Copy(Value, Scratch);
    }

    void emitI64LogicalShiftRightOne(const I64Value &Value) {
      I64Value Scratch = createAnonymousI64Temp("i64_shift_scratch");
      Body.push_back("        CPYBTRLS    " + Scratch.Bytes + "," +
                     Value.Bytes + ",1;");
      emitI64Copy(Value, Scratch);
    }

    void emitI64ArithmeticShiftRightOne(const I64Value &Value) {
      I64Value Scratch = createAnonymousI64Temp("i64_shift_scratch");
      Body.push_back("        CPYBTRAS    " + Scratch.Bytes + "," +
                     Value.Bytes + ",1;");
      emitI64Copy(Value, Scratch);
    }

    void emitI64VariableShift(const I64Value &Dest, const I64Value &LHS,
                              const I64Value &RHS, Instruction::BinaryOps Op) {
      std::string Count = createAnonymousTemp("i64_shift_count");
      GeneratedName LoopName = Names.createBlockName();
      GeneratedName DoneName = Names.createBlockName();
      std::string Loop = LoopName.Name;
      std::string Done = DoneName.Name;
      addMapRecord(LoopName, "i64_shift_loop", "", std::nullopt);
      addMapRecord(DoneName, "i64_shift_done", "", std::nullopt);

      emitI64Copy(Dest, LHS);
      Body.push_back("        CPYNV       " + Count + "," + RHS.Lo + ";");
      Body.push_back(Loop + ":");
      Body.push_back("        CMPNV(B)    " + Count + ",0/EQ(" + Done + ");");
      if (Op == Instruction::Shl)
        emitI64ShiftLeftOne(Dest);
      else if (Op == Instruction::LShr)
        emitI64LogicalShiftRightOne(Dest);
      else if (Op == Instruction::AShr)
        emitI64ArithmeticShiftRightOne(Dest);
      else
        fail("i64 shift operation");
      Body.push_back("        SUBN        " + Count + "," + Count + ",1;");
      Body.push_back("        B           " + Loop + ";");
      Body.push_back(Done + ":");
    }

    void emitI64Bitwise(const I64Value &Dest, const I64Value &LHS,
                        const I64Value &RHS, Instruction::BinaryOps Op) {
      StringRef Opcode;
      switch (Op) {
      case Instruction::And:
        Opcode = "AND";
        break;
      case Instruction::Or:
        Opcode = "OR";
        break;
      case Instruction::Xor:
        Opcode = "XOR";
        break;
      default:
        fail("i64 bitwise operation");
      }
      Body.push_back("        " + Opcode.str() +
                     std::string(11 - Opcode.size(), ' ') + Dest.Bytes + "," +
                     LHS.Bytes + "," + RHS.Bytes + ";");
    }

    std::string emitI64LowBitValue(const I64Value &Source) {
      std::string Bit = createAnonymousTemp("i64_low_bit");
      Body.push_back("        CPYBLA      " + Bit + "B,X'00000001';");
      Body.push_back("        AND         " + Bit + "B," + Source.LoBytes +
                     "," + Bit + "B;");
      return Bit;
    }

    void emitI64UnsignedMul(const I64Value &Dest, const I64Value &LHS,
                            const I64Value &RHS) {
      I64Value Multiplicand = createAnonymousI64Temp("i64_mul_multiplicand");
      I64Value Multiplier = createAnonymousI64Temp("i64_mul_multiplier");
      std::string Count = createAnonymousTemp("i64_mul_count");
      GeneratedName LoopName = Names.createBlockName();
      GeneratedName SkipAddName = Names.createBlockName();
      GeneratedName DoneName = Names.createBlockName();
      std::string Loop = LoopName.Name;
      std::string SkipAdd = SkipAddName.Name;
      std::string Done = DoneName.Name;
      addMapRecord(LoopName, "i64_mul_loop", "", std::nullopt);
      addMapRecord(SkipAddName, "i64_mul_skip_add", "", std::nullopt);
      addMapRecord(DoneName, "i64_mul_done", "", std::nullopt);

      emitI64Zero(Dest);
      emitI64Copy(Multiplicand, LHS);
      emitI64Copy(Multiplier, RHS);
      Body.push_back("        CPYNV       " + Count + ",64;");
      Body.push_back(Loop + ":");
      Body.push_back("        CMPNV(B)    " + Count + ",0/EQ(" + Done + ");");
      std::string LowBit = emitI64LowBitValue(Multiplier);
      Body.push_back("        CMPNV(B)    " + LowBit + ",0/EQ(" + SkipAdd +
                     ");");
      emitI64Add(Dest, Dest, Multiplicand);
      Body.push_back(SkipAdd + ":");
      emitI64ShiftLeftOne(Multiplicand);
      emitI64LogicalShiftRightOne(Multiplier);
      Body.push_back("        SUBN        " + Count + "," + Count + ",1;");
      Body.push_back("        B           " + Loop + ";");
      Body.push_back(Done + ":");
    }

    void emitI64UnsignedDivRem(const I64Value &Quotient,
                               const I64Value &Remainder,
                               const I64Value &Dividend,
                               const I64Value &Divisor) {
      I64Value WorkDividend = createAnonymousI64Temp("i64_div_dividend");
      std::string Count = createAnonymousTemp("i64_div_count");
      GeneratedName LoopName = Names.createBlockName();
      GeneratedName NoTopBitName = Names.createBlockName();
      GeneratedName ReduceName = Names.createBlockName();
      GeneratedName AfterCmpName = Names.createBlockName();
      GeneratedName DoneName = Names.createBlockName();
      std::string Loop = LoopName.Name;
      std::string NoTopBit = NoTopBitName.Name;
      std::string Reduce = ReduceName.Name;
      std::string AfterCmp = AfterCmpName.Name;
      std::string Done = DoneName.Name;
      addMapRecord(LoopName, "i64_div_loop", "", std::nullopt);
      addMapRecord(NoTopBitName, "i64_div_no_top_bit", "", std::nullopt);
      addMapRecord(ReduceName, "i64_div_reduce", "", std::nullopt);
      addMapRecord(AfterCmpName, "i64_div_after_cmp", "", std::nullopt);
      addMapRecord(DoneName, "i64_div_done", "", std::nullopt);

      emitI64Zero(Quotient);
      emitI64Zero(Remainder);
      emitI64Copy(WorkDividend, Dividend);
      Body.push_back("        CPYNV       " + Count + ",64;");
      Body.push_back(Loop + ":");
      Body.push_back("        CMPNV(B)    " + Count + ",0/EQ(" + Done + ");");
      emitI64ShiftLeftOne(Quotient);
      emitI64ShiftLeftOne(Remainder);
      Body.push_back("        CMPNV(B)    " + WorkDividend.Hi + ",0/NLO(" +
                     NoTopBit + ");");
      Body.push_back("        ADDN        " + Remainder.Lo + "," +
                     Remainder.Lo + ",1;");
      Body.push_back(NoTopBit + ":");
      emitI64ShiftLeftOne(WorkDividend);
      emitI64CompareTrueBranch({CmpInst::ICMP_UGE, Remainder, Divisor}, Reduce,
                               AfterCmp);
      Body.push_back("        B           " + AfterCmp + ";");
      Body.push_back(Reduce + ":");
      emitI64Sub(Remainder, Remainder, Divisor);
      Body.push_back("        ADDN        " + Quotient.Lo + "," + Quotient.Lo +
                     ",1;");
      Body.push_back(AfterCmp + ":");
      Body.push_back("        SUBN        " + Count + "," + Count + ",1;");
      Body.push_back("        B           " + Loop + ";");
      Body.push_back(Done + ":");
    }

    void emitI64Abs(const I64Value &Dest, const I64Value &Source,
                    StringRef NegativeFlag) {
      GeneratedName DoneName = Names.createBlockName();
      std::string Done = DoneName.Name;
      addMapRecord(DoneName, "i64_abs_done", "", std::nullopt);

      emitI64Copy(Dest, Source);
      Body.push_back("        CPYNV       " + NegativeFlag.str() + ",0;");
      Body.push_back("        CMPNV(B)    " + Source.Hi + ",0/NLO(" + Done +
                     ");");
      Body.push_back("        CPYNV       " + NegativeFlag.str() + ",1;");
      emitI64Negate(Dest, Source);
      Body.push_back(Done + ":");
    }

    std::string emitXorFlags(StringRef LHS, StringRef RHS) {
      std::string Result = createAnonymousTemp("i64_sign_xor");
      GeneratedName RhsZeroName = Names.createBlockName();
      GeneratedName LhsZeroName = Names.createBlockName();
      GeneratedName DoneName = Names.createBlockName();
      std::string RhsZero = RhsZeroName.Name;
      std::string LhsZero = LhsZeroName.Name;
      std::string Done = DoneName.Name;
      addMapRecord(RhsZeroName, "i64_sign_rhs_zero", "", std::nullopt);
      addMapRecord(LhsZeroName, "i64_sign_lhs_zero", "", std::nullopt);
      addMapRecord(DoneName, "i64_sign_done", "", std::nullopt);

      Body.push_back("        CPYNV       " + Result + "," + LHS.str() + ";");
      Body.push_back("        CMPNV(B)    " + RHS.str() + ",0/EQ(" + RhsZero +
                     ");");
      Body.push_back("        CMPNV(B)    " + LHS.str() + ",0/EQ(" + LhsZero +
                     ");");
      Body.push_back("        CPYNV       " + Result + ",0;");
      Body.push_back("        B           " + Done + ";");
      Body.push_back(LhsZero + ":");
      Body.push_back("        CPYNV       " + Result + ",1;");
      Body.push_back("        B           " + Done + ";");
      Body.push_back(RhsZero + ":");
      Body.push_back(Done + ":");
      return Result;
    }

    void emitI64ConditionalNegate(const I64Value &Value, StringRef Flag) {
      GeneratedName DoneName = Names.createBlockName();
      std::string Done = DoneName.Name;
      addMapRecord(DoneName, "i64_cond_neg_done", "", std::nullopt);
      Body.push_back("        CMPNV(B)    " + Flag.str() + ",0/EQ(" + Done +
                     ");");
      I64Value Negated = createAnonymousI64Temp("i64_negated");
      emitI64Negate(Negated, Value);
      emitI64Copy(Value, Negated);
      Body.push_back(Done + ":");
    }

    void emitI64SignedDivRem(const I64Value &Dest, const I64Value &LHS,
                             const I64Value &RHS, bool WantRemainder) {
      I64Value AbsLHS = createAnonymousI64Temp("i64_abs_lhs");
      I64Value AbsRHS = createAnonymousI64Temp("i64_abs_rhs");
      I64Value Quotient = createAnonymousI64Temp("i64_signed_quotient");
      I64Value Remainder = createAnonymousI64Temp("i64_signed_remainder");
      std::string LHSNegative = createAnonymousTemp("i64_lhs_negative");
      std::string RHSNegative = createAnonymousTemp("i64_rhs_negative");

      emitI64Abs(AbsLHS, LHS, LHSNegative);
      emitI64Abs(AbsRHS, RHS, RHSNegative);
      emitI64UnsignedDivRem(Quotient, Remainder, AbsLHS, AbsRHS);

      if (WantRemainder) {
        emitI64ConditionalNegate(Remainder, LHSNegative);
        emitI64Copy(Dest, Remainder);
        return;
      }

      std::string QuotientNegative = emitXorFlags(LHSNegative, RHSNegative);
      emitI64ConditionalNegate(Quotient, QuotientNegative);
      emitI64Copy(Dest, Quotient);
    }

    void lowerI64BinaryOperator(const BinaryOperator &BO) {
      I64Value Dest = createI64Temp(BO);
      I64Value LHS = getI64Operand(BO.getOperand(0));

      switch (BO.getOpcode()) {
      case Instruction::Add:
        emitI64Add(Dest, LHS, getI64Operand(BO.getOperand(1)));
        break;
      case Instruction::Sub:
        emitI64Sub(Dest, LHS, getI64Operand(BO.getOperand(1)));
        break;
      case Instruction::Mul:
        emitI64UnsignedMul(Dest, LHS, getI64Operand(BO.getOperand(1)));
        break;
      case Instruction::UDiv: {
        I64Value Remainder = createAnonymousI64Temp("i64_udiv_remainder");
        emitI64UnsignedDivRem(Dest, Remainder, LHS,
                              getI64Operand(BO.getOperand(1)));
        break;
      }
      case Instruction::URem: {
        I64Value Quotient = createAnonymousI64Temp("i64_urem_quotient");
        emitI64UnsignedDivRem(Quotient, Dest, LHS,
                              getI64Operand(BO.getOperand(1)));
        break;
      }
      case Instruction::SDiv:
        emitI64SignedDivRem(Dest, LHS, getI64Operand(BO.getOperand(1)),
                            false);
        break;
      case Instruction::SRem:
        emitI64SignedDivRem(Dest, LHS, getI64Operand(BO.getOperand(1)), true);
        break;
      case Instruction::And:
      case Instruction::Or:
      case Instruction::Xor:
        emitI64Bitwise(Dest, LHS, getI64Operand(BO.getOperand(1)),
                       BO.getOpcode());
        break;
      case Instruction::Shl:
        if (isa<ConstantInt>(BO.getOperand(1)))
          Body.push_back("        CPYBTLLS    " + Dest.Bytes + "," +
                         LHS.Bytes + "," +
                         getConstantShiftAmount(BO.getOperand(1), 64) + ";");
        else
          emitI64VariableShift(Dest, LHS, getI64Operand(BO.getOperand(1)),
                               BO.getOpcode());
        break;
      case Instruction::LShr:
        if (isa<ConstantInt>(BO.getOperand(1)))
          Body.push_back("        CPYBTRLS    " + Dest.Bytes + "," +
                         LHS.Bytes + "," +
                         getConstantShiftAmount(BO.getOperand(1), 64) + ";");
        else
          emitI64VariableShift(Dest, LHS, getI64Operand(BO.getOperand(1)),
                               BO.getOpcode());
        break;
      case Instruction::AShr:
        if (isa<ConstantInt>(BO.getOperand(1)))
          Body.push_back("        CPYBTRAS    " + Dest.Bytes + "," +
                         LHS.Bytes + "," +
                         getConstantShiftAmount(BO.getOperand(1), 64) + ";");
        else
          emitI64VariableShift(Dest, LHS, getI64Operand(BO.getOperand(1)),
                               BO.getOpcode());
        break;
      default:
        fail("i64 arithmetic, bitwise, and shift expressions");
      }

      I64Values[&BO] = Dest;
    }

    void lowerAlloca(const AllocaInst &AI) {
      if (AI.isArrayAllocation())
        fail("static scalar or integer aggregate allocas");

      Type *AllocatedTy = AI.getAllocatedType();
      bool ScalarInt = isSupportedInt(AllocatedTy) || isPTR32(AllocatedTy);
      AccessWidth Width =
          ScalarInt ? getAccessWidth(AllocatedTy) : AccessWidth::I8;
      uint32_t Size = 0;
      if (ScalarInt) {
        Size = getAccessWidthBytes(Width);
      } else if (isSupportedArenaAggregateType(AllocatedTy)) {
        Size = static_cast<uint32_t>(DL.getTypeAllocSize(AllocatedTy));
      } else {
        fail("static scalar or integer aggregate allocas");
      }

      uint32_t Alignment = ScalarInt ? 4 : getABIAlignment(DL, AllocatedTy);
      uint32_t Offset =
          ScalarInt ? alignTo4(NextArenaOffset)
                    : static_cast<uint32_t>(alignTo(NextArenaOffset, Alignment));
      if (Offset + Size > ArenaSize)
        fail("phase-1 arena storage within 256 bytes");

      std::string Original = getOriginalName(AI);
      GeneratedName SlotName = Names.createSlotName(Original);
      ArenaSlot Slot{SlotName.Name, Offset, Size, Width,
                     ScalarInt && Width != AccessWidth::I64};
      NextArenaOffset = Offset + Size;
      Slots[&AI] = Slot;
      if (ScalarInt && Width == AccessWidth::I32) {
        Declarations.push_back("DCL     DD          " + Slot.Name +
                               "    BIN(4)     DEF(C_MEM) POS(" +
                               std::to_string(Slot.Offset + 1) + ");");
      } else if (ScalarInt && Width == AccessWidth::I16) {
        Declarations.push_back("DCL     DD          " + Slot.Name +
                               "    BIN(2)     UNSGND DEF(C_MEM) POS(" +
                               std::to_string(Slot.Offset + 1) + ");");
      } else {
        Declarations.push_back("DCL     DD          " + Slot.Name +
                               "    CHAR(" + std::to_string(Size) +
                               ")    DEF(C_MEM) POS(" +
                               std::to_string(Slot.Offset + 1) + ");");
      }
      addMapRecord(SlotName, "local", std::move(Original),
                   getSourceLocation(AI), Slot.Offset, Size, Alignment);
    }

    void lowerBinaryOperator(const BinaryOperator &BO) {
      if (BO.getType()->isIntegerTy(64)) {
        lowerI64BinaryOperator(BO);
        return;
      }

      if (BO.getType()->isIntegerTy(8) || BO.getType()->isIntegerTy(16)) {
        lowerNarrowBinaryOperator(BO);
        return;
      }

      if (!isI32(BO.getType()))
        fail("i32 arithmetic, bitwise, and constant shift expressions");

      std::string Dest = createTemp(BO);
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
      case Instruction::Mul:
        Body.push_back("        MULT        " + Dest + "," + LHS + "," + RHS +
                       ";");
        break;
      case Instruction::SDiv:
      case Instruction::UDiv:
        Body.push_back("        DIV         " + Dest + "," + LHS + "," + RHS +
                       ";");
        break;
      case Instruction::SRem:
      case Instruction::URem:
        Body.push_back("        REM         " + Dest + "," + LHS + "," + RHS +
                       ";");
        break;
      case Instruction::And:
        Body.push_back("        AND         " + Dest + "B," +
                       getI32ByteOperandName(BO.getOperand(0)) + "," +
                       getI32ByteOperandName(BO.getOperand(1)) + ";");
        break;
      case Instruction::Or:
        Body.push_back("        OR          " + Dest + "B," +
                       getI32ByteOperandName(BO.getOperand(0)) + "," +
                       getI32ByteOperandName(BO.getOperand(1)) + ";");
        break;
      case Instruction::Xor:
        Body.push_back("        XOR         " + Dest + "B," +
                       getI32ByteOperandName(BO.getOperand(0)) + "," +
                       getI32ByteOperandName(BO.getOperand(1)) + ";");
        break;
      case Instruction::Shl:
        if (isa<ConstantInt>(BO.getOperand(1)))
          Body.push_back("        CPYBTLLS    " + Dest + "," + LHS + "," +
                         getConstantShiftAmount(BO.getOperand(1)) + ";");
        else
          emitI32VariableShift(Dest, LHS, RHS, BO.getOpcode());
        break;
      case Instruction::LShr:
        if (isa<ConstantInt>(BO.getOperand(1)))
          Body.push_back("        CPYBTRLS    " + Dest + "," + LHS + "," +
                         getConstantShiftAmount(BO.getOperand(1)) + ";");
        else
          emitI32VariableShift(Dest, LHS, RHS, BO.getOpcode());
        break;
      case Instruction::AShr:
        if (isa<ConstantInt>(BO.getOperand(1)))
          Body.push_back("        CPYBTRAS    " + Dest + "," + LHS + "," +
                         getConstantShiftAmount(BO.getOperand(1)) + ";");
        else
          emitI32VariableShift(Dest, LHS, RHS, BO.getOpcode());
        break;
      default:
        fail("i32 arithmetic, bitwise, and shift expressions");
      }

      Values[&BO] = Dest;
    }

    void lowerICmp(const ICmpInst &ICI) {
      if (!isSupportedCompareInt(ICI.getOperand(0)->getType()) ||
          ICI.getOperand(0)->getType() != ICI.getOperand(1)->getType())
        fail("i8/i16/i32/i64 icmp expressions");

      if (ICI.getOperand(0)->getType()->isIntegerTy(64)) {
        I64Comparisons[&ICI] = {ICI.getPredicate(),
                                getI64Operand(ICI.getOperand(0)),
                                getI64Operand(ICI.getOperand(1))};
        return;
      }

      Comparisons[&ICI] = {
          ICI.getPredicate(),
          getCompareOperandName(ICI.getOperand(0), ICI.isSigned()),
          getCompareOperandName(ICI.getOperand(1), ICI.isSigned())};
    }

    void emitI64CompareTrueBranch(const I64CompareValue &Cmp,
                                  StringRef TrueLabel,
                                  StringRef DoneLabel) {
      auto EmitHighLowOrdering = [&](StringRef HiLHS, StringRef HiRHS,
                                     StringRef LowPredicate) {
        Body.push_back("        CMPNV(B)    " + HiLHS.str() + "," +
                       HiRHS.str() + "/LO(" + TrueLabel.str() + ");");
        Body.push_back("        CMPNV(B)    " + HiLHS.str() + "," +
                       HiRHS.str() + "/HI(" + DoneLabel.str() + ");");
        Body.push_back("        CMPNV(B)    " + Cmp.LHS.Lo + "," +
                       Cmp.RHS.Lo + "/" + LowPredicate.str() + "(" +
                       TrueLabel.str() + ");");
      };

      switch (Cmp.Predicate) {
      case CmpInst::ICMP_EQ:
        Body.push_back("        CMPNV(B)    " + Cmp.LHS.Hi + "," +
                       Cmp.RHS.Hi + "/NEQ(" + DoneLabel.str() + ");");
        Body.push_back("        CMPNV(B)    " + Cmp.LHS.Lo + "," +
                       Cmp.RHS.Lo + "/EQ(" + TrueLabel.str() + ");");
        break;
      case CmpInst::ICMP_NE:
        Body.push_back("        CMPNV(B)    " + Cmp.LHS.Hi + "," +
                       Cmp.RHS.Hi + "/NEQ(" + TrueLabel.str() + ");");
        Body.push_back("        CMPNV(B)    " + Cmp.LHS.Lo + "," +
                       Cmp.RHS.Lo + "/NEQ(" + TrueLabel.str() + ");");
        break;
      case CmpInst::ICMP_SLT:
        EmitHighLowOrdering(Cmp.LHS.Hi, Cmp.RHS.Hi, "LO");
        break;
      case CmpInst::ICMP_SLE:
        EmitHighLowOrdering(Cmp.LHS.Hi, Cmp.RHS.Hi, "NHI");
        break;
      case CmpInst::ICMP_SGT:
        Body.push_back("        CMPNV(B)    " + Cmp.LHS.Hi + "," +
                       Cmp.RHS.Hi + "/HI(" + TrueLabel.str() + ");");
        Body.push_back("        CMPNV(B)    " + Cmp.LHS.Hi + "," +
                       Cmp.RHS.Hi + "/LO(" + DoneLabel.str() + ");");
        Body.push_back("        CMPNV(B)    " + Cmp.LHS.Lo + "," +
                       Cmp.RHS.Lo + "/HI(" + TrueLabel.str() + ");");
        break;
      case CmpInst::ICMP_SGE:
        Body.push_back("        CMPNV(B)    " + Cmp.LHS.Hi + "," +
                       Cmp.RHS.Hi + "/HI(" + TrueLabel.str() + ");");
        Body.push_back("        CMPNV(B)    " + Cmp.LHS.Hi + "," +
                       Cmp.RHS.Hi + "/LO(" + DoneLabel.str() + ");");
        Body.push_back("        CMPNV(B)    " + Cmp.LHS.Lo + "," +
                       Cmp.RHS.Lo + "/NLO(" + TrueLabel.str() + ");");
        break;
      case CmpInst::ICMP_ULT:
        EmitHighLowOrdering(Cmp.LHS.HiU, Cmp.RHS.HiU, "LO");
        break;
      case CmpInst::ICMP_ULE:
        EmitHighLowOrdering(Cmp.LHS.HiU, Cmp.RHS.HiU, "NHI");
        break;
      case CmpInst::ICMP_UGT:
        Body.push_back("        CMPNV(B)    " + Cmp.LHS.HiU + "," +
                       Cmp.RHS.HiU + "/HI(" + TrueLabel.str() + ");");
        Body.push_back("        CMPNV(B)    " + Cmp.LHS.HiU + "," +
                       Cmp.RHS.HiU + "/LO(" + DoneLabel.str() + ");");
        Body.push_back("        CMPNV(B)    " + Cmp.LHS.Lo + "," +
                       Cmp.RHS.Lo + "/HI(" + TrueLabel.str() + ");");
        break;
      case CmpInst::ICMP_UGE:
        Body.push_back("        CMPNV(B)    " + Cmp.LHS.HiU + "," +
                       Cmp.RHS.HiU + "/HI(" + TrueLabel.str() + ");");
        Body.push_back("        CMPNV(B)    " + Cmp.LHS.HiU + "," +
                       Cmp.RHS.HiU + "/LO(" + DoneLabel.str() + ");");
        Body.push_back("        CMPNV(B)    " + Cmp.LHS.Lo + "," +
                       Cmp.RHS.Lo + "/NLO(" + TrueLabel.str() + ");");
        break;
      default:
        fail("i64 icmp predicates");
      }
    }

    std::string materializeI64Compare(const ICmpInst &ICI) {
      auto Existing = Values.find(&ICI);
      if (Existing != Values.end())
        return Existing->second;

      auto It = I64Comparisons.find(&ICI);
      if (It == I64Comparisons.end()) {
        lowerICmp(ICI);
        It = I64Comparisons.find(&ICI);
      }

      std::string Dest = createTemp(ICI);
      GeneratedName TrueName = Names.createBlockName();
      GeneratedName DoneName = Names.createBlockName();
      std::string TrueLabel = TrueName.Name;
      std::string DoneLabel = DoneName.Name;
      addMapRecord(TrueName, "i64_bool_true", "", getSourceLocation(ICI));
      addMapRecord(DoneName, "i64_bool_done", "", getSourceLocation(ICI));
      Body.push_back("        CPYNV       " + Dest + ",0;");
      emitI64CompareTrueBranch(It->second, TrueLabel, DoneLabel);
      Body.push_back("        B           " + DoneLabel + ";");
      Body.push_back(TrueLabel + ":");
      Body.push_back("        CPYNV       " + Dest + ",1;");
      Body.push_back(DoneLabel + ":");
      Values[&ICI] = Dest;
      return Dest;
    }

    std::string materializeBoolean(const Value *V) {
      if (const auto *CI = dyn_cast<ConstantInt>(V)) {
        if (!CI->getType()->isIntegerTy(1))
          fail("i1 boolean constants");
        return CI->isOne() ? "1" : "0";
      }

      if (const auto *ICI = dyn_cast<ICmpInst>(V))
        return ICI->getOperand(0)->getType()->isIntegerTy(64)
                   ? materializeI64Compare(*ICI)
                   : materializeCompare(*ICI);

      auto It = Values.find(V);
      if (It == Values.end())
        fail("supported i1 boolean values");
      return It->second;
    }

    void emitBranchOnCondition(const Value *Cond, StringRef TrueLabel) {
      if (const auto *CI = dyn_cast<ConstantInt>(Cond)) {
        if (!CI->getType()->isIntegerTy(1))
          fail("i1 branch conditions");
        if (CI->isOne())
          Body.push_back("        B           " + TrueLabel.str() + ";");
        return;
      }

      if (auto It = Comparisons.find(Cond); It != Comparisons.end()) {
        const CompareValue &Cmp = It->second;
        Body.push_back("        CMPNV(B)    " + Cmp.LHS + "," + Cmp.RHS + "/" +
                       getBranchPredicate(Cmp.Predicate).str() + "(" +
                       TrueLabel.str() + ");");
        return;
      }

      if (auto It = I64Comparisons.find(Cond); It != I64Comparisons.end()) {
        std::string Bool = materializeBoolean(Cond);
        Body.push_back("        CMPNV(B)    " + Bool + ",0/NEQ(" +
                       TrueLabel.str() + ");");
        return;
      }

      if (const auto *ICI = dyn_cast<ICmpInst>(Cond)) {
        lowerICmp(*ICI);
        emitBranchOnCondition(Cond, TrueLabel);
        return;
      }

      std::string Bool = materializeBoolean(Cond);
      Body.push_back("        CMPNV(B)    " + Bool + ",0/NEQ(" +
                     TrueLabel.str() + ");");
    }

    std::string materializeCompare(const ICmpInst &ICI) {
      auto Existing = Values.find(&ICI);
      if (Existing != Values.end())
        return Existing->second;

      auto It = Comparisons.find(&ICI);
      if (It == Comparisons.end()) {
        lowerICmp(ICI);
        It = Comparisons.find(&ICI);
      }

      const CompareValue &Cmp = It->second;
      std::string Dest = createTemp(ICI);
      GeneratedName TrueName = Names.createBlockName();
      GeneratedName DoneName = Names.createBlockName();
      std::string TrueLabel = TrueName.Name;
      std::string DoneLabel = DoneName.Name;
      addMapRecord(TrueName, "bool_true", "", getSourceLocation(ICI));
      addMapRecord(DoneName, "bool_done", "", getSourceLocation(ICI));
      Body.push_back("        CPYNV       " + Dest + ",0;");
      Body.push_back("        CMPNV(B)    " + Cmp.LHS + "," + Cmp.RHS + "/" +
                     getBranchPredicate(Cmp.Predicate).str() + "(" +
                     TrueLabel + ");");
      Body.push_back("        B           " + DoneLabel + ";");
      Body.push_back(TrueLabel + ":");
      Body.push_back("        CPYNV       " + Dest + ",1;");
      Body.push_back(DoneLabel + ":");
      Values[&ICI] = Dest;
      return Dest;
    }

    std::optional<std::string> tryGetPointerOffsetName(const Value *V) const {
      if (isa<ConstantPointerNull>(V))
        return "0";

      if (std::optional<uint32_t> Offset = getBaseArenaOffset(V))
        return std::to_string(*Offset);

      auto It = Values.find(V);
      if (It != Values.end())
        return It->second;

      if (const auto *GEP = dyn_cast<GEPOperator>(V)) {
        const Value *Base = GEP->getPointerOperand();
        APInt ConstantOffset(DL.getIndexSizeInBits(0), 0);
        if (GEP->accumulateConstantOffset(DL, ConstantOffset)) {
          if (std::optional<uint32_t> BaseOffset = getBaseArenaOffset(Base))
            return std::to_string(*BaseOffset + ConstantOffset.getZExtValue());
        }
      }

      return std::nullopt;
    }

    std::string getPointerOffsetName(const Value *V) const {
      if (std::optional<std::string> Offset = tryGetPointerOffsetName(V))
        return *Offset;
      fail("arena PTR32 offsets from allocas, globals, or getelementptr");
      llvm_unreachable("fail should not return");
    }

    std::string addStaticOffset(const Twine &Kind, StringRef Base,
                                uint64_t Offset) {
      if (Offset == 0)
        return Base.str();

      uint64_t BaseValue = 0;
      if (!Base.getAsInteger(10, BaseValue))
        return std::to_string(BaseValue + Offset);

      std::string Dest = createAnonymousTemp(Kind.str());
      Body.push_back("        ADDN        " + Dest + "," + Base.str() + "," +
                     std::to_string(Offset) + ";");
      return Dest;
    }

    void lowerGetElementPtr(const GetElementPtrInst &GEP) {
      std::optional<std::string> BaseOffset =
          tryGetPointerOffsetName(GEP.getPointerOperand());
      if (!BaseOffset)
        fail("getelementptr from arena allocas, globals, or pointer offsets");

      APInt ConstantOffset(DL.getIndexSizeInBits(0), 0);
      if (GEP.accumulateConstantOffset(DL, ConstantOffset)) {
        Values[&GEP] =
            addStaticOffset("ptr_offset", *BaseOffset,
                            ConstantOffset.getZExtValue());
        return;
      }

      const Value *DynamicIndex = nullptr;
      uint64_t DynamicScale = 0;
      uint64_t StaticOffset = 0;
      for (gep_type_iterator GTI = gep_type_begin(GEP), GTE = gep_type_end(GEP);
           GTI != GTE; ++GTI) {
        Value *Index = GTI.getOperand();
        uint64_t Scale = DL.getTypeAllocSize(GTI.getIndexedType());
        if (const auto *CI = dyn_cast<ConstantInt>(Index)) {
          StaticOffset += CI->getZExtValue() * Scale;
          continue;
        }

        if (DynamicIndex)
          fail("getelementptr with one dynamic arena index");
        if (!isI32(Index->getType()))
          fail("i32 getelementptr indexes");
        DynamicIndex = Index;
        DynamicScale = Scale;
      }

      if (!DynamicIndex)
        fail("constant getelementptr offsets");

      std::string DynamicOffset = getOperandName(DynamicIndex);
      if (DynamicScale == 4) {
        std::string Twice = createAnonymousTemp("ptr_offset");
        Body.push_back("        ADDN        " + Twice + "," + DynamicOffset +
                       "," + DynamicOffset + ";");
        std::string Quad = createAnonymousTemp("ptr_offset");
        Body.push_back("        ADDN        " + Quad + "," + Twice + "," +
                       Twice + ";");
        DynamicOffset = Quad;
      } else if (DynamicScale == 2) {
        std::string Twice = createAnonymousTemp("ptr_offset");
        Body.push_back("        ADDN        " + Twice + "," + DynamicOffset +
                       "," + DynamicOffset + ";");
        DynamicOffset = Twice;
      } else if (DynamicScale != 1) {
        std::string Scaled = createAnonymousTemp("ptr_offset");
        Body.push_back("        MULT        " + Scaled + "," + DynamicOffset +
                       "," + std::to_string(DynamicScale) + ";");
        DynamicOffset = Scaled;
      }

      std::string Offset =
          addStaticOffset("ptr_offset", DynamicOffset, StaticOffset);

      uint64_t BaseValue = 0;
      if (!StringRef(*BaseOffset).getAsInteger(10, BaseValue) &&
          BaseValue == 0) {
        Values[&GEP] = Offset;
        return;
      }

      uint64_t OffsetValue = 0;
      if (!StringRef(Offset).getAsInteger(10, OffsetValue) && OffsetValue == 0) {
        Values[&GEP] = *BaseOffset;
        return;
      }

      std::string Dest = createTemp(GEP);
      if (!StringRef(*BaseOffset).getAsInteger(10, BaseValue))
        Body.push_back("        ADDN        " + Dest + "," + Offset + "," +
                       *BaseOffset + ";");
      else
        Body.push_back("        ADDN        " + Dest + "," + *BaseOffset + "," +
                       Offset + ";");
      Values[&GEP] = Dest;
    }

    AccessWidth getAccessWidth(Type *Ty) const {
      if (Ty->isPointerTy())
        return AccessWidth::I32;
      return getIntegerAccessWidth(Ty);
    }

    static StringRef getLensName(AccessWidth Width) {
      switch (Width) {
      case AccessWidth::I8:
        return "LS_I1";
      case AccessWidth::I16:
        return "LS_I2";
      case AccessWidth::I32:
        return "LS_I4";
      case AccessWidth::I64:
        fail("i64 direct load/store lens");
      }
      llvm_unreachable("unknown access width");
    }

    void emitLoadFromReference(StringRef Dest, StringRef Ref,
                               AccessWidth Width) {
      if (Width == AccessWidth::I8) {
        ensureU1Box();
        Body.push_back("        CPYBLA      U1_BOX,X'00000000';");
        Body.push_back("        CPYBLA      U1_BYTE," + Ref.str() + ";");
        Body.push_back("        CPYNV       " + Dest.str() + ",U1_NUM;");
        return;
      }

      Body.push_back("        CPYNV       " + Dest.str() + "," + Ref.str() +
                     ";");
    }

    void emitStoreToReference(StringRef Ref, AccessWidth Width,
                              const Value *Value) {
      if (Width == AccessWidth::I8) {
        if (const auto *CI = dyn_cast<ConstantInt>(Value)) {
          Body.push_back("        CPYBLA      " + Ref.str() + ",X'" +
                         getByteHex(*CI) + "';");
          return;
        }

        ensureU1Box();
        Body.push_back("        CPYNV       U1_NUM," + getOperandName(Value) +
                       ";");
        Body.push_back("        CPYBLA      " + Ref.str() + ",U1_BYTE;");
        return;
      }

      if (Width == AccessWidth::I16) {
        if (const auto *CI = dyn_cast<ConstantInt>(Value)) {
          Body.push_back("        CPYNV       " + Ref.str() + "," +
                         std::to_string(CI->getZExtValue() & 0xFFFF) + ";");
          return;
        }
      }

      Body.push_back("        CPYNV       " + Ref.str() + "," +
                     getOperandName(Value) + ";");
    }

    void emitSetLensPointer(const Value *Ptr) {
      ensureLoadStoreLens();
      emitSetLensPointerFromOffset(getPointerOffsetName(Ptr));
    }

    void emitSetLensPointerFromOffset(StringRef Offset) {
      ensureLoadStoreLens();
      Body.push_back("        CPYNV       OFF," + Offset.str() + ";");
      Body.push_back("        ADDSPP      .LS,.C_BASE,OFF;");
    }

    std::string getPointerOffsetPlus(StringRef Base, uint64_t Offset) {
      return addStaticOffset("ptr_offset", Base, Offset);
    }

    static std::string getByteHex(const ConstantInt &CI) {
      uint32_t Byte = CI.getZExtValue() & 0xFF;
      return getByteHex(Byte);
    }

    static std::string getByteHex(uint32_t Byte) {
      Byte &= 0xFF;
      const char Digits[] = "0123456789ABCDEF";
      std::string Hex;
      Hex.push_back(Digits[Byte >> 4]);
      Hex.push_back(Digits[Byte & 0x0F]);
      return Hex;
    }

    std::optional<SmallVector<uint8_t, 32>>
    getConstantStoreBytes(const Value *V) const {
      auto *C = dyn_cast<Constant>(V);
      if (!C || !isSupportedArenaAggregateType(C->getType()))
        return std::nullopt;

      uint64_t Size = DL.getTypeAllocSize(C->getType());
      if (Size > std::numeric_limits<uint32_t>::max())
        fail("phase-1 aggregate store size");

      SmallVector<uint8_t, 32> Bytes(Size, 0);
      Layout.writeConstantBytes(C, Bytes);
      return Bytes;
    }

    void copyAggregateValueToName(const Value *V, StringRef Dest) {
      if (!isAggregateABIType(V->getType()))
        fail("aggregate call ABI values");

      if (auto AggIt = AggregateValues.find(V); AggIt != AggregateValues.end()) {
        Body.push_back("        CPYBLA      " + Dest.str() + "," +
                       AggIt->second.Name + ";");
        return;
      }

      if (std::optional<SmallVector<uint8_t, 32>> Bytes =
              getConstantStoreBytes(V)) {
        Body.push_back("        CPYBLA      " + Dest.str() + ",X'" +
                       getRawHex(*Bytes) + "';");
        return;
      }

      fail("aggregate call values from constants, loads, or insertvalue");
    }

    ArenaSlot materializeAggregateSlot(const Value *V, StringRef Kind) {
      if (!isAggregateABIType(V->getType()))
        fail("aggregate values");

      if (auto It = AggregateValues.find(V); It != AggregateValues.end())
        return It->second;

      if (std::optional<SmallVector<uint8_t, 32>> Bytes =
              getConstantStoreBytes(V)) {
        ArenaSlot Slot = createAggregateTemp(*V, V->getType(), Kind);
        for (uint64_t I = 0, E = Bytes->size(); I != E; ++I)
          emitStoreByteToPointer(std::to_string(Slot.Offset), I, (*Bytes)[I]);
        AggregateValues[V] = Slot;
        return Slot;
      }

      fail("aggregate values from constants, loads, calls, arguments, or "
           "insertvalue");
      llvm_unreachable("fail should not return");
    }

    void lowerAggregateCompareCall(const CallBase &CB, bool IsEqual) {
      if (CB.arg_size() != 2 ||
          CB.getArgOperand(0)->getType() != CB.getArgOperand(1)->getType())
        fail("OS400MI aggregate compare operands with matching types");

      ArenaSlot LHS =
          materializeAggregateSlot(CB.getArgOperand(0), "aggregate_compare_lhs");
      ArenaSlot RHS =
          materializeAggregateSlot(CB.getArgOperand(1), "aggregate_compare_rhs");
      if (LHS.Size != RHS.Size)
        fail("OS400MI aggregate compare operands with matching layout size");

      std::string Dest = createTemp(CB);
      GeneratedName HitName = Names.createBlockName();
      GeneratedName DoneName = Names.createBlockName();
      std::string HitLabel = HitName.Name;
      std::string DoneLabel = DoneName.Name;
      addMapRecord(HitName,
                   IsEqual ? "aggregate_compare_false"
                           : "aggregate_compare_true",
                   "", getSourceLocation(CB));
      addMapRecord(DoneName, "aggregate_compare_done", "",
                   getSourceLocation(CB));

      Body.push_back("        CPYNV       " + Dest + (IsEqual ? ",1;" : ",0;"));
      for (uint32_t I = 0; I != LHS.Size; ++I) {
        std::string LByte = createAnonymousTemp("aggregate_compare_byte");
        std::string RByte = createAnonymousTemp("aggregate_compare_byte");
        emitSetLensPointerFromOffset(std::to_string(LHS.Offset + I));
        emitLoadFromReference(LByte, "LS_I1", AccessWidth::I8);
        emitSetLensPointerFromOffset(std::to_string(RHS.Offset + I));
        emitLoadFromReference(RByte, "LS_I1", AccessWidth::I8);
        Body.push_back("        CMPNV(B)    " + LByte + "," + RByte +
                       "/NEQ(" + HitLabel + ");");
      }

      Body.push_back("        B           " + DoneLabel + ";");
      Body.push_back(HitLabel + ":");
      Body.push_back("        CPYNV       " + Dest + (IsEqual ? ",0;" : ",1;"));
      Body.push_back(DoneLabel + ":");
      Values[&CB] = Dest;
    }

    void emitStoreByteToPointer(StringRef PtrOffset, uint64_t ByteOffset,
                                uint8_t Byte) {
      emitSetLensPointerFromOffset(getPointerOffsetPlus(PtrOffset, ByteOffset));
      Body.push_back("        CPYBLA      LS_I1,X'" + getByteHex(Byte) + "';");
    }

    void emitStoreBytesToPointer(const Value *Ptr, ArrayRef<uint8_t> Bytes) {
      std::string Base = getPointerOffsetName(Ptr);
      for (uint64_t I = 0, E = Bytes.size(); I != E; ++I)
        emitStoreByteToPointer(Base, I, Bytes[I]);
    }

    void emitCopyBytesByOffset(StringRef DestBase, StringRef SourceBase,
                               uint64_t Size) {
      ensureU1Box();
      for (uint64_t I = 0; I != Size; ++I) {
        emitSetLensPointerFromOffset(getPointerOffsetPlus(SourceBase, I));
        Body.push_back("        CPYBLA      U1_BYTE,LS_I1;");
        emitSetLensPointerFromOffset(getPointerOffsetPlus(DestBase, I));
        Body.push_back("        CPYBLA      LS_I1,U1_BYTE;");
      }
    }

    void emitCopyBytes(const Value *DestPtr, const Value *SourcePtr,
                       uint64_t Size) {
      emitCopyBytesByOffset(getPointerOffsetName(DestPtr),
                            getPointerOffsetName(SourcePtr), Size);
    }

    void emitFillBytesByOffset(StringRef DestBase, uint64_t Size,
                               const Value *Byte) {
      std::optional<std::string> ConstantHex;
      if (const auto *CI = dyn_cast<ConstantInt>(Byte))
        ConstantHex = getByteHex(*CI);
      else {
        ensureU1Box();
        Body.push_back("        CPYNV       U1_NUM," + getOperandName(Byte) +
                       ";");
      }

      for (uint64_t I = 0; I != Size; ++I) {
        emitSetLensPointerFromOffset(getPointerOffsetPlus(DestBase, I));
        if (ConstantHex)
          Body.push_back("        CPYBLA      LS_I1,X'" + *ConstantHex + "';");
        else
          Body.push_back("        CPYBLA      LS_I1,U1_BYTE;");
      }
    }

    void emitFillBytes(const Value *DestPtr, uint64_t Size, const Value *Byte) {
      emitFillBytesByOffset(getPointerOffsetName(DestPtr), Size, Byte);
    }

    std::string getMemoryLengthName(const Value *V) {
      if (const auto *CI = dyn_cast<ConstantInt>(V))
        return std::to_string(CI->getZExtValue());

      if (V->getType()->isIntegerTy(64))
        return getI64Operand(V).Lo;

      if (V->getType()->isIntegerTy(32))
        return getOperandName(V);

      fail("i32 or i64 byte counts for memory operations");
      llvm_unreachable("fail should not return");
    }

    void emitCopyBytesLoop(const Value *DestPtr, const Value *SourcePtr,
                           const Value *Length) {
      ensureU1Box();
      std::string Dest = createAnonymousTemp("memcpy_dest");
      std::string Source = createAnonymousTemp("memcpy_source");
      std::string Count = createAnonymousTemp("memcpy_count");
      GeneratedName LoopName = Names.createBlockName();
      GeneratedName DoneName = Names.createBlockName();
      std::string Loop = LoopName.Name;
      std::string Done = DoneName.Name;
      addMapRecord(LoopName, "memcpy_loop", "", std::nullopt);
      addMapRecord(DoneName, "memcpy_done", "", std::nullopt);

      Body.push_back("        CPYNV       " + Dest + "," +
                     getPointerOffsetName(DestPtr) + ";");
      Body.push_back("        CPYNV       " + Source + "," +
                     getPointerOffsetName(SourcePtr) + ";");
      Body.push_back("        CPYNV       " + Count + "," +
                     getMemoryLengthName(Length) + ";");
      Body.push_back(Loop + ":");
      Body.push_back("        CMPNV(B)    " + Count + ",0/EQ(" + Done + ");");
      emitSetLensPointerFromOffset(Source);
      Body.push_back("        CPYBLA      U1_BYTE,LS_I1;");
      emitSetLensPointerFromOffset(Dest);
      Body.push_back("        CPYBLA      LS_I1,U1_BYTE;");
      Body.push_back("        ADDN        " + Source + "," + Source + ",1;");
      Body.push_back("        ADDN        " + Dest + "," + Dest + ",1;");
      Body.push_back("        SUBN        " + Count + "," + Count + ",1;");
      Body.push_back("        B           " + Loop + ";");
      Body.push_back(Done + ":");
    }

    void emitFillBytesLoop(const Value *DestPtr, const Value *Length,
                           const Value *Byte) {
      std::string Dest = createAnonymousTemp("memset_dest");
      std::string Count = createAnonymousTemp("memset_count");
      std::optional<std::string> ConstantHex;
      if (const auto *CI = dyn_cast<ConstantInt>(Byte))
        ConstantHex = getByteHex(*CI);
      else {
        ensureU1Box();
        Body.push_back("        CPYNV       U1_NUM," + getOperandName(Byte) +
                       ";");
      }

      GeneratedName LoopName = Names.createBlockName();
      GeneratedName DoneName = Names.createBlockName();
      std::string Loop = LoopName.Name;
      std::string Done = DoneName.Name;
      addMapRecord(LoopName, "memset_loop", "", std::nullopt);
      addMapRecord(DoneName, "memset_done", "", std::nullopt);

      Body.push_back("        CPYNV       " + Dest + "," +
                     getPointerOffsetName(DestPtr) + ";");
      Body.push_back("        CPYNV       " + Count + "," +
                     getMemoryLengthName(Length) + ";");
      Body.push_back(Loop + ":");
      Body.push_back("        CMPNV(B)    " + Count + ",0/EQ(" + Done + ");");
      emitSetLensPointerFromOffset(Dest);
      if (ConstantHex)
        Body.push_back("        CPYBLA      LS_I1,X'" + *ConstantHex + "';");
      else
        Body.push_back("        CPYBLA      LS_I1,U1_BYTE;");
      Body.push_back("        ADDN        " + Dest + "," + Dest + ",1;");
      Body.push_back("        SUBN        " + Count + "," + Count + ",1;");
      Body.push_back("        B           " + Loop + ";");
      Body.push_back(Done + ":");
    }

    void emitLoadI64FromOffset(const I64Value &Dest, StringRef SourceBase) {
      for (uint64_t I = 0; I != 8; ++I) {
        emitSetLensPointerFromOffset(getPointerOffsetPlus(SourceBase, I));
        Body.push_back("        CPYBLA      " + Dest.Byte[I] + ",LS_I1;");
      }
    }

    void emitStoreI64ToOffset(StringRef DestBase, const I64Value &Source) {
      for (uint64_t I = 0; I != 8; ++I) {
        emitSetLensPointerFromOffset(getPointerOffsetPlus(DestBase, I));
        Body.push_back("        CPYBLA      LS_I1," + Source.Byte[I] + ";");
      }
    }

    static std::optional<uint64_t> getConstantLength(const Value *V) {
      const auto *CI = dyn_cast<ConstantInt>(V);
      if (!CI)
        return std::nullopt;
      return CI->getZExtValue();
    }

    void lowerMemCpy(const MemCpyInst &MI) {
      if (std::optional<uint64_t> Size = getConstantLength(MI.getLength())) {
        emitCopyBytes(MI.getDest(), MI.getSource(), *Size);
        return;
      }
      emitCopyBytesLoop(MI.getDest(), MI.getSource(), MI.getLength());
    }

    void lowerMemSet(const MemSetInst &MI) {
      if (std::optional<uint64_t> Size = getConstantLength(MI.getLength())) {
        emitFillBytes(MI.getDest(), *Size, MI.getValue());
        return;
      }
      emitFillBytesLoop(MI.getDest(), MI.getLength(), MI.getValue());
    }

    void lowerStore(const StoreInst &SI) {
      Type *ValueTy = SI.getValueOperand()->getType();
      if (ValueTy->isIntegerTy(64)) {
        I64Value Source = getI64Operand(SI.getValueOperand());
        emitStoreI64ToOffset(getPointerOffsetName(SI.getPointerOperand()),
                             Source);
        return;
      }

      if (!isSupportedInt(ValueTy) && !isPTR32(ValueTy)) {
        auto AggIt = AggregateValues.find(SI.getValueOperand());
        if (AggIt != AggregateValues.end()) {
          emitCopyBytesByOffset(getPointerOffsetName(SI.getPointerOperand()),
                                std::to_string(AggIt->second.Offset),
                                AggIt->second.Size);
          return;
        }

        std::optional<SmallVector<uint8_t, 32>> Bytes =
            getConstantStoreBytes(SI.getValueOperand());
        if (!Bytes)
          fail("integer aggregate values from constants or aggregate loads");
        emitStoreBytesToPointer(SI.getPointerOperand(), *Bytes);
        return;
      }

      AccessWidth Width = getAccessWidth(ValueTy);
      if (std::optional<ArenaSlot> Slot =
              getDirectScalarSlot(SI.getPointerOperand())) {
        if (Slot->Width != Width)
          fail("stores matching direct scalar slot width");
        emitStoreToReference(Slot->Name, Width, SI.getValueOperand());
        return;
      }

      emitSetLensPointer(SI.getPointerOperand());
      emitStoreToReference(getLensName(Width), Width, SI.getValueOperand());
    }

    void lowerLoad(const LoadInst &LI) {
      if (LI.getType()->isIntegerTy(64)) {
        I64Value Dest = createI64Temp(LI);
        emitLoadI64FromOffset(Dest, getPointerOffsetName(LI.getPointerOperand()));
        I64Values[&LI] = Dest;
        return;
      }

      if (!isSupportedInt(LI.getType()) && !isPTR32(LI.getType())) {
        ArenaSlot Slot = createAggregateTemp(LI, LI.getType(), "aggregate_temp");
        emitCopyBytesByOffset(std::to_string(Slot.Offset),
                              getPointerOffsetName(LI.getPointerOperand()),
                              Slot.Size);
        AggregateValues[&LI] = Slot;
        return;
      }

      AccessWidth Width = getAccessWidth(LI.getType());

      std::string Dest = createTemp(LI);
      if (std::optional<ArenaSlot> Slot =
              getDirectScalarSlot(LI.getPointerOperand())) {
        if (Slot->Width != Width)
          fail("loads matching direct scalar slot width");
        emitLoadFromReference(Dest, Slot->Name, Width);
        Values[&LI] = Dest;
        return;
      }

      emitSetLensPointer(LI.getPointerOperand());
      emitLoadFromReference(Dest, getLensName(Width), Width);
      Values[&LI] = Dest;
    }

    void lowerExtractValue(const ExtractValueInst &EVI) {
      auto AggIt = AggregateValues.find(EVI.getAggregateOperand());
      if (AggIt == AggregateValues.end())
        fail("extractvalue from supported aggregate values");

      Type *ResultTy = EVI.getType();
      uint64_t Offset =
          AggIt->second.Offset +
          getIndexedOffset(EVI.getAggregateOperand()->getType(),
                           EVI.getIndices());

      if (isSupportedInt(ResultTy) || isPTR32(ResultTy)) {
        if (ResultTy->isIntegerTy(64)) {
          I64Value Dest = createI64Temp(EVI);
          emitLoadI64FromOffset(Dest, std::to_string(Offset));
          I64Values[&EVI] = Dest;
          return;
        }

        AccessWidth Width = getAccessWidth(ResultTy);
        std::string Dest = createTemp(EVI);
        emitSetLensPointerFromOffset(std::to_string(Offset));
        emitLoadFromReference(Dest, getLensName(Width), Width);
        Values[&EVI] = Dest;
        return;
      }

      if (!isSupportedArenaValueType(ResultTy))
        fail("extractvalue scalar or integer aggregate results");

      ArenaSlot Slot = createAggregateTemp(EVI, ResultTy, "aggregate_temp");
      emitCopyBytesByOffset(std::to_string(Slot.Offset), std::to_string(Offset),
                            Slot.Size);
      AggregateValues[&EVI] = Slot;
    }

    void lowerInsertValue(const InsertValueInst &IVI) {
      Type *AggTy = IVI.getType();
      if (!isSupportedArenaValueType(AggTy))
        fail("insertvalue into integer array/struct aggregate values");

      ArenaSlot Slot = createAggregateTemp(IVI, AggTy, "aggregate_temp");
      const Value *Base = IVI.getAggregateOperand();
      if (auto AggIt = AggregateValues.find(Base);
          AggIt != AggregateValues.end()) {
        emitCopyBytesByOffset(std::to_string(Slot.Offset),
                              std::to_string(AggIt->second.Offset),
                              Slot.Size);
      } else if (auto *C = dyn_cast<Constant>(Base)) {
        if (isa<UndefValue>(C))
          emitFillBytesByOffset(std::to_string(Slot.Offset), Slot.Size,
                                ConstantInt::get(Type::getInt8Ty(IVI.getContext()),
                                                 0));
        else if (std::optional<SmallVector<uint8_t, 32>> Bytes =
                     getConstantStoreBytes(C))
          for (uint64_t I = 0, E = Bytes->size(); I != E; ++I)
            emitStoreByteToPointer(std::to_string(Slot.Offset), I, (*Bytes)[I]);
        else
          fail("insertvalue base aggregate constants");
      } else {
        fail("insertvalue base aggregate values");
      }

      uint64_t Offset = Slot.Offset + getIndexedOffset(AggTy, IVI.getIndices());
      const Value *Inserted = IVI.getInsertedValueOperand();
      if (isSupportedInt(Inserted->getType()) || isPTR32(Inserted->getType())) {
        if (Inserted->getType()->isIntegerTy(64)) {
          emitStoreI64ToOffset(std::to_string(Offset), getI64Operand(Inserted));
          AggregateValues[&IVI] = Slot;
          return;
        }

        AccessWidth Width = getAccessWidth(Inserted->getType());
        emitSetLensPointerFromOffset(std::to_string(Offset));
        emitStoreToReference(getLensName(Width), Width, Inserted);
      } else {
        auto InsertedAggIt = AggregateValues.find(Inserted);
        if (InsertedAggIt == AggregateValues.end())
          fail("insertvalue inserted scalar or aggregate values");
        emitCopyBytesByOffset(std::to_string(Offset),
                              std::to_string(InsertedAggIt->second.Offset),
                              InsertedAggIt->second.Size);
      }

      AggregateValues[&IVI] = Slot;
    }

    void lowerZExt(const ZExtInst &ZI) {
      Type *SrcTy = ZI.getOperand(0)->getType();
      if (ZI.getType()->isIntegerTy(64)) {
        if (!(SrcTy->isIntegerTy(1) || SrcTy->isIntegerTy(8) ||
              SrcTy->isIntegerTy(16) || SrcTy->isIntegerTy(32)))
          fail("zext from i1/i8/i16/i32 to i64");

        if (const auto *CI = dyn_cast<ConstantInt>(ZI.getOperand(0))) {
          auto *DestTy = cast<IntegerType>(ZI.getType());
          I64Value Dest = materializeI64Constant(
              *cast<ConstantInt>(
                  ConstantInt::get(DestTy, CI->getValue().zext(64))));
          I64Values[&ZI] = Dest;
          return;
        }

        I64Value Dest = createI64Temp(ZI);
        Body.push_back("        CPYBLA      " + Dest.Bytes + ",X'0000000000000000';");
        if (SrcTy->isIntegerTy(32))
          Body.push_back("        CPYBLA      " + Dest.LoBytes + "," +
                         getI32ByteOperandName(ZI.getOperand(0)) + ";");
        else
          Body.push_back("        CPYNV       " + Dest.Lo + "," +
                         getOperandName(ZI.getOperand(0)) + ";");
        I64Values[&ZI] = Dest;
        return;
      }

      if (!ZI.getType()->isIntegerTy(32) ||
          !(SrcTy->isIntegerTy(1) || SrcTy->isIntegerTy(8) ||
            SrcTy->isIntegerTy(16)))
        fail("zext from i1/i8/i16 to i32");

      if (const auto *ICI = dyn_cast<ICmpInst>(ZI.getOperand(0))) {
        Values[&ZI] = ICI->getOperand(0)->getType()->isIntegerTy(64)
                          ? materializeI64Compare(*ICI)
                          : materializeCompare(*ICI);
        return;
      }

      Values[&ZI] = getOperandName(ZI.getOperand(0));
    }

    void lowerSExt(const SExtInst &SI) {
      Type *SrcTy = SI.getOperand(0)->getType();
      if (SI.getType()->isIntegerTy(64)) {
        if (!(SrcTy->isIntegerTy(8) || SrcTy->isIntegerTy(16) ||
              SrcTy->isIntegerTy(32)))
          fail("sext from i8/i16/i32 to i64");

        if (const auto *CI = dyn_cast<ConstantInt>(SI.getOperand(0))) {
          auto *DestTy = cast<IntegerType>(SI.getType());
          I64Value Dest = materializeI64Constant(
              *cast<ConstantInt>(
                  ConstantInt::get(DestTy, CI->getValue().sext(64))));
          I64Values[&SI] = Dest;
          return;
        }

        const Value *Source = SI.getOperand(0);
        std::string Src =
            SrcTy->isIntegerTy(32) ? getOperandName(Source)
                                   : materializeSignedNarrowOperand(Source);
        I64Value Dest = createI64Temp(SI);
        GeneratedName DoneName = Names.createBlockName();
        std::string DoneLabel = DoneName.Name;
        addMapRecord(DoneName, "i64_sext_done", "", getSourceLocation(SI));
        Body.push_back("        CPYBLA      " + Dest.Bytes + ",X'0000000000000000';");
        if (SrcTy->isIntegerTy(32))
          Body.push_back("        CPYBLA      " + Dest.LoBytes + "," +
                         getI32ByteOperandName(Source) + ";");
        else
          Body.push_back("        CPYBLA      " + Dest.LoBytes + "," + Src +
                         "B;");
        Body.push_back("        CMPNV(B)    " + Src + ",0/NLO(" + DoneLabel +
                       ");");
        Body.push_back("        CPYNV       " + Dest.Hi + ",-1;");
        Body.push_back(DoneLabel + ":");
        I64Values[&SI] = Dest;
        return;
      }

      if (!SI.getType()->isIntegerTy(32) ||
          !(SrcTy->isIntegerTy(8) || SrcTy->isIntegerTy(16)))
        fail("sext from i8/i16 to i32");

      if (const auto *CI = dyn_cast<ConstantInt>(SI.getOperand(0))) {
        Values[&SI] = std::to_string(static_cast<int32_t>(CI->getSExtValue()));
        return;
      }

      std::string Src = getOperandName(SI.getOperand(0));
      std::string Dest = createTemp(SI);
      GeneratedName DoneName = Names.createBlockName();
      std::string DoneLabel = DoneName.Name;
      addMapRecord(DoneName, "sext_done", "", getSourceLocation(SI));

      uint32_t SignLimit = SrcTy->isIntegerTy(8) ? 127 : 32767;
      uint32_t Modulus = SrcTy->isIntegerTy(8) ? 256 : 65536;
      Body.push_back("        CPYNV       " + Dest + "," + Src + ";");
      Body.push_back("        CMPNV(B)    " + Src + "," +
                     std::to_string(SignLimit) + "/NHI(" + DoneLabel + ");");
      Body.push_back("        SUBN        " + Dest + "," + Src + "," +
                     std::to_string(Modulus) + ";");
      Body.push_back(DoneLabel + ":");
      Values[&SI] = Dest;
    }

    void lowerTrunc(const TruncInst &TI) {
      if (!TI.getOperand(0)->getType()->isIntegerTy(64) ||
          !TI.getType()->isIntegerTy(32))
        fail("trunc from i64 to i32");

      I64Value Src = getI64Operand(TI.getOperand(0));
      std::string Dest = createTemp(TI);
      Body.push_back("        CPYBLA      " + Dest + "B," + Src.LoBytes + ";");
      Values[&TI] = Dest;
    }

    void lowerSelect(const SelectInst &SI) {
      if (SI.getType()->isIntegerTy(64)) {
        I64Value Dest = createI64Temp(SI);
        GeneratedName TrueName = Names.createBlockName();
        GeneratedName DoneName = Names.createBlockName();
        std::string TrueLabel = TrueName.Name;
        std::string DoneLabel = DoneName.Name;
        addMapRecord(TrueName, "select_true", "", getSourceLocation(SI));
        addMapRecord(DoneName, "select_done", "", getSourceLocation(SI));
        Body.push_back("        CPYBLA      " + Dest.Bytes + "," +
                       getI64Operand(SI.getFalseValue()).Bytes + ";");
        emitBranchOnCondition(SI.getCondition(), TrueLabel);
        Body.push_back("        B           " + DoneLabel + ";");
        Body.push_back(TrueLabel + ":");
        Body.push_back("        CPYBLA      " + Dest.Bytes + "," +
                       getI64Operand(SI.getTrueValue()).Bytes + ";");
        Body.push_back(DoneLabel + ":");
        I64Values[&SI] = Dest;
        return;
      }

      if (!SI.getType()->isIntegerTy(1) && !isI32Like(SI.getType()))
        fail("i1/i32/PTR32 select values");
      std::string Dest = createTemp(SI);
      GeneratedName TrueName = Names.createBlockName();
      GeneratedName DoneName = Names.createBlockName();
      std::string TrueLabel = TrueName.Name;
      std::string DoneLabel = DoneName.Name;
      addMapRecord(TrueName, "select_true", "", getSourceLocation(SI));
      addMapRecord(DoneName, "select_done", "", getSourceLocation(SI));
      Body.push_back("        CPYNV       " + Dest + "," +
                     getOperandName(SI.getFalseValue()) + ";");
      emitBranchOnCondition(SI.getCondition(), TrueLabel);
      Body.push_back("        B           " + DoneLabel + ";");
      Body.push_back(TrueLabel + ":");
      Body.push_back("        CPYNV       " + Dest + "," +
                     getOperandName(SI.getTrueValue()) + ";");
      Body.push_back(DoneLabel + ":");
      Values[&SI] = Dest;
    }

    void lowerCall(const CallBase &CB) {
      if (CB.isInlineAsm()) {
        validateRawInlineAsm(CB);
        const auto *IA = cast<InlineAsm>(CB.getCalledOperand());
        appendRawAsmLines(Body, IA->getAsmString());
        return;
      }

      if (CB.isIndirectCall())
        fail("direct function calls; indirect calls require a function pointer "
             "ABI");

      const Function *Callee = CB.getCalledFunction();
      if (std::optional<bool> IsAggregateCompare =
              getAggregateComparePseudoKind(Callee)) {
        lowerAggregateCompareCall(CB, *IsAggregateCompare);
        return;
      }

      if (isa<IntrinsicInst>(CB))
        fail("supported LLVM intrinsics");

      if (!Callee || Callee->isDeclaration())
        fail("defined internal callees; external calls require CALLX ABI");

      const FunctionInfo &CalleeInfo = FunctionPlan.getInfo(*Callee);
      if (!(CB.getType()->isIntegerTy(1) || CB.getType()->isIntegerTy(8) ||
            CB.getType()->isIntegerTy(16) || CB.getType()->isIntegerTy(32) ||
            CB.getType()->isIntegerTy(64) || CB.getType()->isPointerTy() ||
            isAggregateABIType(CB.getType())))
        fail("direct i1/i8/i16/i32/i64/PTR32/aggregate function call results");
      if (CB.arg_size() != CalleeInfo.Args.size())
        fail("direct function call arguments matching callee signature");

      for (unsigned I = 0, E = CB.arg_size(); I != E; ++I) {
        const Value *Arg = CB.getArgOperand(I);
        const FunctionInfo::Slot &ArgSlot = CalleeInfo.Args[I];
        if (ArgSlot.Ty != Arg->getType())
          fail("direct function call arguments matching callee signature");

        if (Arg->getType()->isIntegerTy(64)) {
          Body.push_back("        CPYBLA      " + ArgSlot.I64.Bytes + "," +
                         getI64Operand(Arg).Bytes + ";");
          continue;
        }

        if (isAggregateABIType(Arg->getType())) {
          copyAggregateValueToName(Arg, ArgSlot.Name);
          continue;
        }

        Body.push_back("        CPYNV       " + ArgSlot.Name + "," +
                       getOperandName(Arg) + ";");
        maskIntegerSlotToType(ArgSlot.Name, ArgSlot.Ty);
      }

      invalidateCallAliasedMemory(CB, CalleeInfo);
      Body.push_back("        CALLI       " + CalleeInfo.EntryName +
                     ", *, " + CalleeInfo.ReturnPointerName + ";");
      if (CB.getType()->isIntegerTy(64)) {
        I64Value Dest = createI64Temp(CB, "call_result");
        Body.push_back("        CPYBLA      " + Dest.Bytes + "," +
                       CalleeInfo.ReturnSlot.I64.Bytes + ";");
        I64Values[&CB] = Dest;
        return;
      }

      if (isAggregateABIType(CB.getType())) {
        ArenaSlot Dest = createAggregateTemp(CB, CB.getType(), "call_result");
        Body.push_back("        CPYBLA      " + Dest.Name + "," +
                       CalleeInfo.ReturnSlot.Name + ";");
        AggregateValues[&CB] = Dest;
        return;
      }

      std::string Dest = createTemp(CB);
      Body.push_back("        CPYNV       " + Dest + "," +
                     CalleeInfo.ReturnSlot.Name + ";");
      maskIntegerSlotToType(Dest, CB.getType());
      Values[&CB] = Dest;
    }

    void lowerReturn(const ReturnInst &RI) {
      Value *Ret = RI.getReturnValue();
      if (!Ret || Ret->getType() != CurrentFunction->ReturnSlot.Ty)
        fail("returns matching lowered function signature");
      if (Ret->getType()->isIntegerTy(64)) {
        Body.push_back("        CPYBLA      " +
                       CurrentFunction->ReturnSlot.I64.Bytes + "," +
                       getI64Operand(Ret).Bytes + ";");
      } else if (isAggregateABIType(Ret->getType())) {
        copyAggregateValueToName(Ret, CurrentFunction->ReturnSlot.Name);
      } else {
        Body.push_back("        CPYNV       " +
                       CurrentFunction->ReturnSlot.Name + "," +
                       getOperandName(Ret) + ";");
        maskIntegerSlotToType(CurrentFunction->ReturnSlot.Name,
                              CurrentFunction->ReturnSlot.Ty);
      }
      Body.push_back("        B           " + CurrentFunction->ReturnPointerName +
                     ";");
    }

    void lowerUnconditionalBranch(const UncondBrInst &BI) {
      Body.push_back("        B           " +
                     getEdgeTarget(BI.getParent(), BI.getSuccessor(0)) + ";");
    }

    void lowerConditionalBranch(const CondBrInst &BI) {
      emitBranchOnCondition(
          BI.getCondition(), getEdgeTarget(BI.getParent(), BI.getSuccessor(0)));
      Body.push_back("        B           " +
                     getEdgeTarget(BI.getParent(), BI.getSuccessor(1)) +
                     ";");
    }

    bool hasPHIs(const BasicBlock *BB) const {
      auto It = BlockPHIs.find(BB);
      return It != BlockPHIs.end() && !It->second.empty();
    }

    std::string getEdgeTarget(const BasicBlock *Pred,
                              const BasicBlock *Succ) {
      if (!hasPHIs(Succ))
        return getBlockLabel(Succ);

      GeneratedName EdgeName = Names.createBlockName();
      std::string EdgeLabel = EdgeName.Name;
      addMapRecord(EdgeName, "edge_block", "",
                   getSourceLocation(*Succ));
      EdgeBlocks.push_back(EdgeLabel + ":");
      for (const PHINode *PN : BlockPHIs[Succ]) {
        Value *Incoming = PN->getIncomingValueForBlock(Pred);
        if (PN->getType()->isIntegerTy(64))
          EdgeBlocks.push_back("        CPYBLA      " +
                               getI64Operand(PN).Bytes + "," +
                               getI64Operand(Incoming).Bytes + ";");
        else
          EdgeBlocks.push_back("        CPYNV       " + getOperandName(PN) +
                               "," + getOperandName(Incoming) + ";");
      }
      EdgeBlocks.push_back("        B           " + getBlockLabel(Succ) + ";");
      return EdgeLabel;
    }

  public:
    FunctionLowerer(const DataLayout &DL, const ArenaLayout &Layout,
                    const ModuleFunctionPlan &FunctionPlan,
                    NameAllocator &Names)
        : Names(Names), DL(DL), Layout(Layout), FunctionPlan(FunctionPlan),
          NextArenaOffset(Layout.getNextArenaOffset()) {}

    void lower(const Function &F) {
      CurrentFunction = &FunctionPlan.getInfo(F);
      Values.clear();
      I64Values.clear();
      AggregateValues.clear();
      SignedNarrowValues.clear();
      Comparisons.clear();
      I64Comparisons.clear();
      Slots.clear();
      BlockLabels.clear();
      BlockPHIs.clear();
      EdgeBlocks.clear();

      Body.push_back("ENTRY " + CurrentFunction->EntryName + " INT;");

      uint32_t FrameOffset = NextArenaOffset;
      unsigned ArgIndex = 0;
      for (const Argument &Arg : F.args()) {
        const FunctionInfo::Slot &ArgSlot = CurrentFunction->Args[ArgIndex++];
        if (Arg.getType() != ArgSlot.Ty)
          fail("function arguments matching lowered function signature");
        if (Arg.getType()->isIntegerTy(64)) {
          I64Values[&Arg] = ArgSlot.I64;
        } else if (isAggregateABIType(Arg.getType())) {
          ArenaSlot Slot =
              createAggregateTemp(Arg, Arg.getType(), "aggregate_arg");
          Body.push_back("        CPYBLA      " + Slot.Name + "," +
                         ArgSlot.Name + ";");
          AggregateValues[&Arg] = Slot;
        } else {
          maskIntegerSlotToType(ArgSlot.Name, ArgSlot.Ty);
          Values[&Arg] = ArgSlot.Name;
        }
      }

      for (const BasicBlock &BB : F) {
        std::string Original = getOriginalName(BB);
        GeneratedName Label = Names.createBlockName(Original);
        addMapRecord(Label, "basic_block", std::move(Original),
                     getSourceLocation(BB));
        BlockLabels[&BB] = Label.Name;
        for (const PHINode &PN : BB.phis()) {
          if (!PN.getType()->isIntegerTy(1) && !isI32Like(PN.getType()) &&
              !PN.getType()->isIntegerTy(64))
            fail("i1/i32/PTR32/i64 phi nodes");
          BlockPHIs[&BB].push_back(&PN);
          if (PN.getType()->isIntegerTy(64))
            I64Values[&PN] = createI64Temp(PN);
          else
            Values[&PN] = createTemp(PN);
        }
      }

      for (const BasicBlock &BB : F) {
        Body.push_back(getBlockLabel(&BB) + ":");

        bool SawTerminator = false;
        for (const Instruction &I : BB) {
          if (SawTerminator)
            fail("instructions before a single final terminator per block");

          if (isa<PHINode>(&I)) {
            continue;
          } else if (const auto *AI = dyn_cast<AllocaInst>(&I)) {
            lowerAlloca(*AI);
          } else if (const auto *GEP = dyn_cast<GetElementPtrInst>(&I)) {
            lowerGetElementPtr(*GEP);
          } else if (const auto *BO = dyn_cast<BinaryOperator>(&I)) {
            lowerBinaryOperator(*BO);
          } else if (const auto *ICI = dyn_cast<ICmpInst>(&I)) {
            lowerICmp(*ICI);
          } else if (const auto *ZI = dyn_cast<ZExtInst>(&I)) {
            lowerZExt(*ZI);
          } else if (const auto *SI = dyn_cast<SExtInst>(&I)) {
            lowerSExt(*SI);
          } else if (const auto *TI = dyn_cast<TruncInst>(&I)) {
            lowerTrunc(*TI);
          } else if (const auto *SI = dyn_cast<SelectInst>(&I)) {
            lowerSelect(*SI);
          } else if (const auto *SI = dyn_cast<StoreInst>(&I)) {
            lowerStore(*SI);
          } else if (const auto *LI = dyn_cast<LoadInst>(&I)) {
            lowerLoad(*LI);
          } else if (const auto *EVI = dyn_cast<ExtractValueInst>(&I)) {
            lowerExtractValue(*EVI);
          } else if (const auto *IVI = dyn_cast<InsertValueInst>(&I)) {
            lowerInsertValue(*IVI);
          } else if (const auto *MI = dyn_cast<MemCpyInst>(&I)) {
            lowerMemCpy(*MI);
          } else if (const auto *MI = dyn_cast<MemSetInst>(&I)) {
            lowerMemSet(*MI);
          } else if (const auto *CB = dyn_cast<CallBase>(&I)) {
            lowerCall(*CB);
          } else if (const auto *RI = dyn_cast<ReturnInst>(&I)) {
            lowerReturn(*RI);
            SawTerminator = true;
          } else if (const auto *BI = dyn_cast<UncondBrInst>(&I)) {
            lowerUnconditionalBranch(*BI);
            SawTerminator = true;
          } else if (const auto *BI = dyn_cast<CondBrInst>(&I)) {
            lowerConditionalBranch(*BI);
            SawTerminator = true;
          } else {
            fail("supported scalar alloca/getelementptr/store/load/add/sub/icmp/zext/sext/select/"
                 "phi, extractvalue, insertvalue, memcpy, memset, branch, and "
                 "ret instructions");
          }
        }

        if (!SawTerminator)
          fail("each lowered function block ending in ret or branch");
      }

      for (const std::string &Line : EdgeBlocks)
        Body.push_back(Line);

      addFrameMapRecord(FrameOffset, NextArenaOffset - FrameOffset);
    }

    ArrayRef<std::string> getDeclarations() const { return Declarations; }
    ArrayRef<std::string> getBody() const { return Body; }
    ArrayRef<MapRecord> getMapRecords() const { return MapRecords; }
  };

  void emitProgram(const Module &M, const ArenaLayout &Layout,
                   const ModuleFunctionPlan &Plan,
                   const FunctionLowerer &Lowerer) {
    SmallVector<std::string, 16> ModuleAsmLines;
    appendRawAsmLines(ModuleAsmLines, M.getModuleInlineAsm());
    for (const std::string &Line : ModuleAsmLines)
      OS << Line << "\n";
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
    OS << "DCL     DD          C_MEM      CHAR(" << Layout.getArenaSize()
       << ") BDRY(16);\n";
    OS << "DCL     SPCPTR      .C_BASE    INIT(C_MEM);\n";
    for (const std::string &Decl : Layout.getDeclarations())
      OS << Decl << "\n";
    for (const std::string &Decl : Plan.getDeclarations())
      OS << Decl << "\n";
    for (const std::string &Decl : Lowerer.getDeclarations())
      OS << Decl << "\n";
    OS << "DCL     INSPTR      .MAIN;\n";
    OS << "ENTRY * (PARM_LIST) EXT;\n";
    OS << "        STPLLEN     NBR_PARMS;\n";
    OS << "        CALLI       MAIN, *, .MAIN;\n";
    OS << "        RTX         *;\n";
    for (const std::string &Line : Lowerer.getBody())
      OS << Line << "\n";
    OS << "        PEND;\n";
  }

  static MapRecord makeFixedMapRecord(
      std::string MIName, std::string Kind, std::string NameClass,
      std::string Original = "",
      std::optional<SourceLocationRecord> SourceLocation = std::nullopt,
      std::optional<uint32_t> ArenaOffset = std::nullopt,
      std::optional<uint32_t> Size = std::nullopt,
      std::optional<uint32_t> Alignment = std::nullopt) {
    if (MIName.size() > NameAllocator::getMaxMINameLength())
      fail("fixed MI names no longer than 48 characters");

    MapRecord Record;
    Record.MIName = std::move(MIName);
    Record.Kind = std::move(Kind);
    Record.NameClass = std::move(NameClass);
    Record.Original = std::move(Original);
    Record.MaxNameLength = NameAllocator::getMaxMINameLength();
    Record.SourceLocation = std::move(SourceLocation);
    Record.ArenaOffset = ArenaOffset;
    Record.Size = Size;
    Record.Alignment = Alignment;
    return Record;
  }

  void emitMapRecord(raw_ostream &MapOS, const MapRecord &Record) {
    MapOS << "{\"mi_name\":";
    writeJSONString(MapOS, Record.MIName);
    MapOS << ",\"kind\":";
    writeJSONString(MapOS, Record.Kind);
    if (!Record.NameClass.empty()) {
      MapOS << ",\"name_class\":";
      writeJSONString(MapOS, Record.NameClass);
    }
    if (Record.NameOrdinal)
      MapOS << ",\"name_ordinal\":" << *Record.NameOrdinal;
    if (Record.MaxNameLength)
      MapOS << ",\"max_name_length\":" << *Record.MaxNameLength;
    MapOS << ",\"collision\":" << (Record.Collision ? "true" : "false");
    if (Record.CollisionOrdinal)
      MapOS << ",\"collision_ordinal\":" << *Record.CollisionOrdinal;
    if (!Record.Hash.empty()) {
      MapOS << ",\"hash\":";
      writeJSONString(MapOS, Record.Hash);
    }
    if (!Record.Original.empty()) {
      MapOS << ",\"original\":";
      writeJSONString(MapOS, Record.Original);
    }
    if (Record.SourceLocation) {
      MapOS << ",\"source_location\":{";
      bool NeedComma = false;
      if (!Record.SourceLocation->File.empty()) {
        MapOS << "\"file\":";
        writeJSONString(MapOS, Record.SourceLocation->File);
        NeedComma = true;
      }
      if (Record.SourceLocation->Line) {
        if (NeedComma)
          MapOS << ",";
        MapOS << "\"line\":" << *Record.SourceLocation->Line;
        NeedComma = true;
      }
      if (Record.SourceLocation->Column) {
        if (NeedComma)
          MapOS << ",";
        MapOS << "\"column\":" << *Record.SourceLocation->Column;
      }
      MapOS << "}";
    }
    if (Record.ArenaOffset)
      MapOS << ",\"arena_offset\":" << *Record.ArenaOffset;
    if (Record.Size)
      MapOS << ",\"size\":" << *Record.Size;
    if (Record.Alignment)
      MapOS << ",\"alignment\":" << *Record.Alignment;
    if (!Record.Encoding.empty()) {
      MapOS << ",\"encoding\":";
      writeJSONString(MapOS, Record.Encoding);
    }
    MapOS << "}\n";
  }

  void emitMapFile(const ArenaLayout &Layout, const ModuleFunctionPlan &Plan,
                   const FunctionLowerer &Lowerer) {
    if (OutputFilename.empty() || OutputFilename == "-")
      return;

    std::string MapFilename = OutputFilename + ".jsonl";
    std::error_code EC;
    raw_fd_ostream MapOS(MapFilename, EC, sys::fs::OF_Text);
    if (EC)
      report_fatal_error(Twine("failed to open OS400MI map file '") +
                             MapFilename + "': " + EC.message(),
                         false);

    emitMapRecord(MapOS,
                  makeFixedMapRecord("C_MEM", "arena", "reserved", "", {},
                                     0, Layout.getArenaSize(), 16));
    emitMapRecord(MapOS, makeFixedMapRecord("MAIN_RC", "return_slot",
                                            "reserved", "", {}, std::nullopt,
                                            4, 4));
    for (const MapRecord &Record : Plan.getMapRecords())
      emitMapRecord(MapOS, Record);
    for (const MapRecord &Record : Layout.getMapRecords())
      emitMapRecord(MapOS, Record);
    for (const MapRecord &Record : Lowerer.getMapRecords())
      emitMapRecord(MapOS, Record);
  }

public:
  static char ID;

  explicit OS400MIEmitPass(raw_ostream &OS, StringRef OutputFilename)
      : ModulePass(ID), OS(OS), OutputFilename(OutputFilename.str()) {}

  StringRef getPassName() const override { return "OS400MI MI Source Emitter"; }

  bool runOnModule(Module &M) override {
    NameAllocator Names;
    Names.reserveName("ARGC@");
    Names.reserveName("ARGV@");
    Names.reserveName("PARM_LIST");
    Names.reserveName("ARGC");
    Names.reserveName("ARGV");
    Names.reserveName("NBR_PARMS");
    Names.reserveName("MAIN_RC");
    Names.reserveName("C_MEM");
    Names.reserveName(".C_BASE");
    Names.reserveName("MAIN");
    Names.reserveName(".MAIN");

    ModuleFunctionPlan FunctionPlan(Names);
    FunctionPlan.analyze(M);

    ArenaLayout Layout(M.getDataLayout(), Names);
    Layout.lower(M);
    FunctionLowerer Lowerer(M.getDataLayout(), Layout, FunctionPlan, Names);
    for (const Function *F : FunctionPlan.getFunctions())
      Lowerer.lower(*F);
    emitProgram(M, Layout, FunctionPlan, Lowerer);
    emitMapFile(Layout, FunctionPlan, Lowerer);
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
    PM.add(new OS400MIEmitPass(Out, Options.ObjectFilenameForDebug));
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
