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

    return *Main;
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

    GeneratedName createSlotName(StringRef Original = "") {
      return allocate("local", "S", Original);
    }

    GeneratedName createTempName(StringRef Original = "") {
      return allocate("temp", "T", Original);
    }

    GeneratedName createBlockName(StringRef Original = "") {
      return allocate("label", "B", Original);
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

  enum class AccessWidth : uint8_t { I8 = 1, I16 = 2, I32 = 4 };

  static AccessWidth getIntegerAccessWidth(Type *Ty) {
    if (Ty->isIntegerTy(8))
      return AccessWidth::I8;
    if (Ty->isIntegerTy(16))
      return AccessWidth::I16;
    if (Ty->isIntegerTy(32))
      return AccessWidth::I32;
    fail("i8/i16/i32 arena accesses");
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
    if (Ty->isIntegerTy(8) || Ty->isIntegerTy(16) || Ty->isIntegerTy(32))
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
            CI->getType()->isIntegerTy(32)))
        fail("i8/i16/i32 integer aggregate constants");
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

  struct CompareValue {
    CmpInst::Predicate Predicate;
    std::string LHS;
    std::string RHS;
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

    NameAllocator Names;
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

    void layoutIntegerGlobal(const GlobalVariable &GV) {
      const auto *CI = dyn_cast<ConstantInt>(GV.getInitializer());
      if (!CI || !(CI->getType()->isIntegerTy(8) ||
                   CI->getType()->isIntegerTy(16) ||
                   CI->getType()->isIntegerTy(32)))
        fail("i8/i16/i32 globals with constant integer initializers");

      AccessWidth Width = getIntegerAccessWidth(CI->getType());
      uint32_t Size = getAccessWidthBytes(Width);
      uint32_t Alignment = Width == AccessWidth::I8 ? 1 : Size;
      uint32_t Offset = alignTo(NextArenaOffset, Alignment);
      if (Offset + Size > ArenaSize)
        fail("phase-1 arena storage within 256 bytes");

      std::string Original = getOriginalName(GV);
      GeneratedName Name = Names.createGlobalName(Original);
      int32_t InitialValue = static_cast<int32_t>(CI->getSExtValue());
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
      } else {
        Declarations.push_back("DCL     DD          " + Name.Name +
                               "    BIN(4)     DEF(C_MEM) POS(" +
                               std::to_string(Offset + 1) + ") INIT(" +
                               std::to_string(InitialValue) + ");");
      }
      addGlobalObject(GV, Name, "global", Offset, Size, Alignment, Width, true);
      addMapRecord(Name, "global", std::move(Original), Offset, Size, Alignment,
                   getSourceLocation(GV));
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
      writeConstantBytes(DL, GV.getInitializer(), Bytes);

      std::string Original = getOriginalName(GV);
      GeneratedName Name = Names.createGlobalName(Original);
      Declarations.push_back("DCL     DD          " + Name.Name + "    CHAR(" +
                             std::to_string(Size) +
                             ")    DEF(C_MEM) POS(" +
                             std::to_string(Offset + 1) + ") INIT(X'" +
                             getRawHex(Bytes) + "');");
      addGlobalObject(GV, Name, "aggregate", Offset, Size, Alignment,
                      AccessWidth::I8, false);
      addMapRecord(Name, "aggregate", std::move(Original), Offset, Size,
                   Alignment, getSourceLocation(GV), "raw-bytes");
      NextArenaOffset = Offset + Size;
    }

  public:
    explicit ArenaLayout(const DataLayout &DL) : DL(DL) {}

    void lower(const Module &M) {
      for (const GlobalVariable &GV : M.globals()) {
        if (GV.isDeclaration())
          fail("defined globals only");

        Type *ValueTy = GV.getValueType();
        if (ValueTy->isIntegerTy(8) || ValueTy->isIntegerTy(16) ||
            ValueTy->isIntegerTy(32)) {
          layoutIntegerGlobal(GV);
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

        fail("i8/i16/i32 globals, constant i8 string globals, and integer "
             "array/struct globals");
      }
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

  class FunctionLowerer {
    NameAllocator Names;
    const DataLayout &DL;
    const ArenaLayout &Layout;
    DenseMap<const Value *, std::string> Values;
    DenseMap<const Value *, std::string> SignedNarrowValues;
    DenseMap<const Value *, CompareValue> Comparisons;
    DenseMap<const AllocaInst *, ArenaSlot> Slots;
    DenseMap<const BasicBlock *, std::string> BlockLabels;
    DenseMap<const BasicBlock *, SmallVector<const PHINode *, 2>> BlockPHIs;
    SmallVector<std::string, 8> Declarations;
    SmallVector<std::string, 16> Body;
    SmallVector<std::string, 16> EdgeBlocks;
    SmallVector<MapRecord, 16> MapRecords;
    uint32_t NextArenaOffset;
    bool HasLoadStoreLens = false;
    bool HasU1Box = false;

    static bool isI32(Type *Ty) { return Ty && Ty->isIntegerTy(32); }
    static bool isSupportedInt(Type *Ty) {
      return Ty && (Ty->isIntegerTy(8) || Ty->isIntegerTy(16) ||
                    Ty->isIntegerTy(32));
    }

    static bool isSupportedCompareInt(Type *Ty) {
      return Ty && (Ty->isIntegerTy(8) || Ty->isIntegerTy(16) ||
                    Ty->isIntegerTy(32));
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

    std::string getOperandName(const Value *V) const {
      if (const auto *CI = dyn_cast<ConstantInt>(V)) {
        if (!CI->getType()->isIntegerTy(1) && !isSupportedInt(CI->getType()))
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
      addMapRecord(Name, Kind.str(), "", std::nullopt, std::nullopt, 4, 4);
      return Name.Name;
    }

    void lowerAlloca(const AllocaInst &AI) {
      if (AI.isArrayAllocation())
        fail("static scalar or integer aggregate allocas");

      Type *AllocatedTy = AI.getAllocatedType();
      bool ScalarInt = isSupportedInt(AllocatedTy);
      AccessWidth Width =
          ScalarInt ? getIntegerAccessWidth(AllocatedTy) : AccessWidth::I8;
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
      ArenaSlot Slot{SlotName.Name, Offset, Size, Width, ScalarInt};
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
      if (!isI32(BO.getType()))
        fail("i32 add/sub expressions");

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
      default:
        fail("i32 add/sub expressions");
      }

      Values[&BO] = Dest;
    }

    void lowerICmp(const ICmpInst &ICI) {
      if (!isSupportedCompareInt(ICI.getOperand(0)->getType()) ||
          ICI.getOperand(0)->getType() != ICI.getOperand(1)->getType())
        fail("i8/i16/i32 icmp expressions");

      Comparisons[&ICI] = {
          ICI.getPredicate(),
          getCompareOperandName(ICI.getOperand(0), ICI.isSigned()),
          getCompareOperandName(ICI.getOperand(1), ICI.isSigned())};
    }

    std::string materializeBoolean(const Value *V) {
      if (const auto *CI = dyn_cast<ConstantInt>(V)) {
        if (!CI->getType()->isIntegerTy(1))
          fail("i1 boolean constants");
        return CI->isOne() ? "1" : "0";
      }

      if (const auto *ICI = dyn_cast<ICmpInst>(V))
        return materializeCompare(*ICI);

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
        fail("dynamic getelementptr scale 1, 2, or 4");
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
      writeConstantBytes(DL, C, Bytes);
      return Bytes;
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

    void emitCopyBytes(const Value *DestPtr, const Value *SourcePtr,
                       uint64_t Size) {
      std::string DestBase = getPointerOffsetName(DestPtr);
      std::string SourceBase = getPointerOffsetName(SourcePtr);
      ensureU1Box();
      for (uint64_t I = 0; I != Size; ++I) {
        emitSetLensPointerFromOffset(getPointerOffsetPlus(SourceBase, I));
        Body.push_back("        CPYBLA      U1_BYTE,LS_I1;");
        emitSetLensPointerFromOffset(getPointerOffsetPlus(DestBase, I));
        Body.push_back("        CPYBLA      LS_I1,U1_BYTE;");
      }
    }

    void emitFillBytes(const Value *DestPtr, uint64_t Size, const Value *Byte) {
      std::string DestBase = getPointerOffsetName(DestPtr);
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

    static uint64_t getConstantLength(const Value *V) {
      const auto *CI = dyn_cast<ConstantInt>(V);
      if (!CI)
        fail("constant byte counts for aggregate memory operations");
      return CI->getZExtValue();
    }

    void lowerMemCpy(const MemCpyInst &MI) {
      uint64_t Size = getConstantLength(MI.getLength());
      emitCopyBytes(MI.getDest(), MI.getSource(), Size);
    }

    void lowerMemSet(const MemSetInst &MI) {
      uint64_t Size = getConstantLength(MI.getLength());
      emitFillBytes(MI.getDest(), Size, MI.getValue());
    }

    void lowerStore(const StoreInst &SI) {
      Type *ValueTy = SI.getValueOperand()->getType();
      if (!isSupportedInt(ValueTy)) {
        std::optional<SmallVector<uint8_t, 32>> Bytes =
            getConstantStoreBytes(SI.getValueOperand());
        if (!Bytes)
          fail("constant integer aggregate stores");
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

    void lowerZExt(const ZExtInst &ZI) {
      Type *SrcTy = ZI.getOperand(0)->getType();
      if (!ZI.getType()->isIntegerTy(32) ||
          !(SrcTy->isIntegerTy(1) || SrcTy->isIntegerTy(8) ||
            SrcTy->isIntegerTy(16)))
        fail("zext from i1/i8/i16 to i32");

      if (const auto *ICI = dyn_cast<ICmpInst>(ZI.getOperand(0))) {
        Values[&ZI] = materializeCompare(*ICI);
        return;
      }

      Values[&ZI] = getOperandName(ZI.getOperand(0));
    }

    void lowerSExt(const SExtInst &SI) {
      Type *SrcTy = SI.getOperand(0)->getType();
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

    void lowerSelect(const SelectInst &SI) {
      if (!SI.getType()->isIntegerTy(1) && !isI32(SI.getType()))
        fail("i1/i32 select values");
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

    void lowerReturn(const ReturnInst &RI) {
      Value *Ret = RI.getReturnValue();
      if (!Ret || !isI32(Ret->getType()))
        fail("i32 returns from main");
      Body.push_back("        CPYNV       MAIN_RC," + getOperandName(Ret) + ";");
      Body.push_back("        B           .MAIN;");
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
        EdgeBlocks.push_back("        CPYNV       " + getOperandName(PN) + "," +
                             getOperandName(Incoming) + ";");
      }
      EdgeBlocks.push_back("        B           " + getBlockLabel(Succ) + ";");
      return EdgeLabel;
    }

  public:
    FunctionLowerer(const DataLayout &DL, const ArenaLayout &Layout)
        : DL(DL), Layout(Layout),
          NextArenaOffset(Layout.getNextArenaOffset()) {}

    void lower(const Function &F) {
      for (const BasicBlock &BB : F) {
        std::string Original = getOriginalName(BB);
        GeneratedName Label = Names.createBlockName(Original);
        addMapRecord(Label, "basic_block", std::move(Original),
                     getSourceLocation(BB));
        BlockLabels[&BB] = Label.Name;
        for (const PHINode &PN : BB.phis()) {
          if (!PN.getType()->isIntegerTy(1) && !isI32(PN.getType()))
            fail("i1/i32 phi nodes");
          BlockPHIs[&BB].push_back(&PN);
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
          } else if (const auto *SI = dyn_cast<SelectInst>(&I)) {
            lowerSelect(*SI);
          } else if (const auto *SI = dyn_cast<StoreInst>(&I)) {
            lowerStore(*SI);
          } else if (const auto *LI = dyn_cast<LoadInst>(&I)) {
            lowerLoad(*LI);
          } else if (const auto *MI = dyn_cast<MemCpyInst>(&I)) {
            lowerMemCpy(*MI);
          } else if (const auto *MI = dyn_cast<MemSetInst>(&I)) {
            lowerMemSet(*MI);
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
                 "phi, memcpy, memset, branch, and ret instructions");
          }
        }

        if (!SawTerminator)
          fail("each main block ending in ret or branch");
      }

      for (const std::string &Line : EdgeBlocks)
        Body.push_back(Line);
    }

    ArrayRef<std::string> getDeclarations() const { return Declarations; }
    ArrayRef<std::string> getBody() const { return Body; }
    ArrayRef<MapRecord> getMapRecords() const { return MapRecords; }
  };

  void emitProgram(const ArenaLayout &Layout, const FunctionLowerer &Lowerer) {
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

  void emitMapFile(const ArenaLayout &Layout, const FunctionLowerer &Lowerer,
                   const Function &Main) {
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
    emitMapRecord(MapOS,
                  makeFixedMapRecord("MAIN", "function", "function", "main",
                                     getSourceLocation(Main)));
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
    const Function &Main = getMainFunction(M);
    ArenaLayout Layout(M.getDataLayout());
    Layout.lower(M);
    FunctionLowerer Lowerer(M.getDataLayout(), Layout);
    Lowerer.lower(Main);
    emitProgram(Layout, Lowerer);
    emitMapFile(Layout, Lowerer, Main);
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
