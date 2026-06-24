//===--- OS400MI.cpp - Emit OS/400 MI builtins ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CodeGenFunction.h"
#include "clang/Basic/TargetBuiltins.h"
#include "llvm/IR/Function.h"

using namespace clang;
using namespace CodeGen;

static llvm::Value *emitOS400MICall(CodeGenFunction &CGF, llvm::StringRef Name,
                                    llvm::Type *ReturnTy,
                                    llvm::ArrayRef<llvm::Value *> Args) {
  llvm::SmallVector<llvm::Type *, 4> ArgTys;
  for (llvm::Value *Arg : Args)
    ArgTys.push_back(Arg->getType());

  llvm::FunctionType *FnTy =
      llvm::FunctionType::get(ReturnTy, ArgTys, /*isVarArg=*/false);
  llvm::FunctionCallee Callee = CGF.CGM.CreateRuntimeFunction(FnTy, Name);
  return CGF.Builder.CreateCall(Callee, Args);
}

llvm::Value *CodeGenFunction::EmitOS400MIBuiltinExpr(unsigned BuiltinID,
                                                     const CallExpr *E) {
  llvm::Type *RetVoidTy = VoidTy;
  llvm::Type *PtrTy = Int8PtrTy;
  llvm::Type *I16Ty = Int16Ty;

  llvm::SmallVector<llvm::Value *, 4> Args;
  for (const Expr *Arg : E->arguments())
    Args.push_back(EmitScalarExpr(Arg));

  switch (BuiltinID) {
  case OS400MI::BI__builtin_os400mi_ufcb:
    return emitOS400MICall(*this, "llvm.os400mi.ufcb", PtrTy, Args);
  case OS400MI::BI__builtin_os400mi_ufcb_outbuf:
    return emitOS400MICall(*this, "llvm.os400mi.ufcb.outbuf", PtrTy, Args);
  case OS400MI::BI__builtin_os400mi_ufcb_file:
    return emitOS400MICall(*this, "llvm.os400mi.ufcb.file", PtrTy, Args);
  case OS400MI::BI__builtin_os400mi_ufcb_library:
    return emitOS400MICall(*this, "llvm.os400mi.ufcb.library", PtrTy, Args);
  case OS400MI::BI__builtin_os400mi_ufcb_member:
    return emitOS400MICall(*this, "llvm.os400mi.ufcb.member", PtrTy, Args);
  case OS400MI::BI__builtin_os400mi_ufcb_odp:
    return emitOS400MICall(*this, "llvm.os400mi.ufcb.odp", PtrTy, Args);
  case OS400MI::BI__builtin_os400mi_odp_dcb_put:
    return emitOS400MICall(*this, "llvm.os400mi.odp.dcb.put", I16Ty, Args);
  case OS400MI::BI__builtin_os400mi_sysptr_sept:
    return emitOS400MICall(*this, "llvm.os400mi.sysptr.sept", PtrTy, Args);
  case OS400MI::BI__builtin_os400mi_sysptr_program:
    return emitOS400MICall(*this, "llvm.os400mi.sysptr.program", PtrTy, Args);
  case OS400MI::BI__builtin_os400mi_spcptr_null:
    return emitOS400MICall(*this, "llvm.os400mi.spcptr.null", PtrTy, Args);
  case OS400MI::BI__builtin_os400mi_spcptr_add:
    return emitOS400MICall(*this, "llvm.os400mi.spcptr.add", PtrTy, Args);
  case OS400MI::BI__builtin_os400mi_native_char:
    return emitOS400MICall(*this, "llvm.os400mi.native.char", PtrTy, Args);
  case OS400MI::BI__builtin_os400mi_native_bin4:
    return emitOS400MICall(*this, "llvm.os400mi.native.bin4", PtrTy, Args);
  case OS400MI::BI__builtin_os400mi_native_bin4_set:
    return emitOS400MICall(*this, "llvm.os400mi.native.bin4.set", RetVoidTy,
                           Args);
  case OS400MI::BI__builtin_os400mi_native_bin4_get:
    return emitOS400MICall(*this, "llvm.os400mi.native.bin4.get", IntTy, Args);
  case OS400MI::BI__builtin_os400mi_dm_put_wait_option:
    return emitOS400MICall(*this, "llvm.os400mi.dm.put.wait.option", PtrTy,
                           Args);
  case OS400MI::BI__builtin_os400mi_char_fill:
    return emitOS400MICall(*this, "llvm.os400mi.char.fill", RetVoidTy, Args);
  case OS400MI::BI__builtin_os400mi_char_from_cstr:
    return emitOS400MICall(*this, "llvm.os400mi.char.from.cstr", RetVoidTy,
                           Args);
  case OS400MI::BI__builtin_os400mi_char_from_cstr_blank_padded:
    return emitOS400MICall(*this, "llvm.os400mi.char.from.cstr.blank.padded",
                           RetVoidTy, Args);
  case OS400MI::BI__builtin_os400mi_char_to_cstr:
    return emitOS400MICall(*this, "llvm.os400mi.char.to.cstr", RetVoidTy,
                           Args);
  case OS400MI::BI__builtin_os400mi_ol0:
    return emitOS400MICall(*this, "llvm.os400mi.ol.0", PtrTy, Args);
  case OS400MI::BI__builtin_os400mi_ol1:
    return emitOS400MICall(*this, "llvm.os400mi.ol.1", PtrTy, Args);
  case OS400MI::BI__builtin_os400mi_ol2:
    return emitOS400MICall(*this, "llvm.os400mi.ol.2", PtrTy, Args);
  case OS400MI::BI__builtin_os400mi_ol3:
    return emitOS400MICall(*this, "llvm.os400mi.ol.3", PtrTy, Args);
  case OS400MI::BI__builtin_os400mi_ol4:
    return emitOS400MICall(*this, "llvm.os400mi.ol.4", PtrTy, Args);
  case OS400MI::BI__builtin_os400mi_ol5:
    return emitOS400MICall(*this, "llvm.os400mi.ol.5", PtrTy, Args);
  case OS400MI::BI__builtin_os400mi_ol6:
    return emitOS400MICall(*this, "llvm.os400mi.ol.6", PtrTy, Args);
  case OS400MI::BI__builtin_os400mi_ol7:
    return emitOS400MICall(*this, "llvm.os400mi.ol.7", PtrTy, Args);
  case OS400MI::BI__builtin_os400mi_ol8:
    return emitOS400MICall(*this, "llvm.os400mi.ol.8", PtrTy, Args);
  case OS400MI::BI__builtin_os400mi_callx:
    return emitOS400MICall(*this, "llvm.os400mi.callx", RetVoidTy, Args);
  case OS400MI::BI__builtin_os400mi_callx0:
    return emitOS400MICall(*this, "llvm.os400mi.callx.0", RetVoidTy, Args);
  case OS400MI::BI__builtin_os400mi_callx1:
    return emitOS400MICall(*this, "llvm.os400mi.callx.1", RetVoidTy, Args);
  case OS400MI::BI__builtin_os400mi_callx2:
    return emitOS400MICall(*this, "llvm.os400mi.callx.2", RetVoidTy, Args);
  case OS400MI::BI__builtin_os400mi_callx3:
    return emitOS400MICall(*this, "llvm.os400mi.callx.3", RetVoidTy, Args);
  default:
    llvm_unreachable("unexpected OS400MI builtin");
  }
}
