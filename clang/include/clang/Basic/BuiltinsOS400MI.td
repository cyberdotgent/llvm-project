//===--- BuiltinsOS400MI.td - OS/400 MI builtins database ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "clang/Basic/BuiltinsBase.td"

class OS400MIBuiltin<string prototype> : TargetBuiltin {
  let Prototype = prototype;
  let Attributes = [NoThrow];
}

def Ufcb : OS400MIBuiltin<"void *()"> {
  let Spellings = ["__builtin_os400mi_ufcb"];
}

def UfcbOutbuf : OS400MIBuiltin<"void *(void *)"> {
  let Spellings = ["__builtin_os400mi_ufcb_outbuf"];
}

def UfcbFile : OS400MIBuiltin<"void *(void *)"> {
  let Spellings = ["__builtin_os400mi_ufcb_file"];
}

def UfcbLibrary : OS400MIBuiltin<"void *(void *)"> {
  let Spellings = ["__builtin_os400mi_ufcb_library"];
}

def UfcbMember : OS400MIBuiltin<"void *(void *)"> {
  let Spellings = ["__builtin_os400mi_ufcb_member"];
}

def UfcbOdp : OS400MIBuiltin<"void *(void *)"> {
  let Spellings = ["__builtin_os400mi_ufcb_odp"];
}

def OdpDcbPut : OS400MIBuiltin<"unsigned short(void *)"> {
  let Spellings = ["__builtin_os400mi_odp_dcb_put"];
}

def SysptrSept : OS400MIBuiltin<"void *(unsigned short)"> {
  let Spellings = ["__builtin_os400mi_sysptr_sept"];
}

def SpcptrNull : OS400MIBuiltin<"void *()"> {
  let Spellings = ["__builtin_os400mi_spcptr_null"];
}

def SpcptrAdd : OS400MIBuiltin<"void *(void *, unsigned int)"> {
  let Spellings = ["__builtin_os400mi_spcptr_add"];
}

def DmPutWaitOption : OS400MIBuiltin<"void *()"> {
  let Spellings = ["__builtin_os400mi_dm_put_wait_option"];
}

def CharFill : OS400MIBuiltin<"void(void *, unsigned int, int)"> {
  let Spellings = ["__builtin_os400mi_char_fill"];
}

def CharFromCstr : OS400MIBuiltin<"void(void *, unsigned int, char const *)"> {
  let Spellings = ["__builtin_os400mi_char_from_cstr"];
}

def CharFromCstrBlankPadded
    : OS400MIBuiltin<"void(void *, unsigned int, char const *)"> {
  let Spellings = ["__builtin_os400mi_char_from_cstr_blank_padded"];
}

def Callx0 : OS400MIBuiltin<"void(void *)"> {
  let Spellings = ["__builtin_os400mi_callx0"];
}

def Callx1 : OS400MIBuiltin<"void(void *, void *)"> {
  let Spellings = ["__builtin_os400mi_callx1"];
}

def Callx2 : OS400MIBuiltin<"void(void *, void *, void *)"> {
  let Spellings = ["__builtin_os400mi_callx2"];
}

def Callx3 : OS400MIBuiltin<"void(void *, void *, void *, void *)"> {
  let Spellings = ["__builtin_os400mi_callx3"];
}
