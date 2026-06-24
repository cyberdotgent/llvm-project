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

def SysptrProgram : OS400MIBuiltin<"void *(char const *, char const *)"> {
  let Spellings = ["__builtin_os400mi_sysptr_program"];
}

def SpcptrNull : OS400MIBuiltin<"void *()"> {
  let Spellings = ["__builtin_os400mi_spcptr_null"];
}

def SpcptrAdd : OS400MIBuiltin<"void *(void *, unsigned int)"> {
  let Spellings = ["__builtin_os400mi_spcptr_add"];
}

def NativeChar : OS400MIBuiltin<"void *(unsigned int)"> {
  let Spellings = ["__builtin_os400mi_native_char"];
}

def NativeBin4 : OS400MIBuiltin<"void *()"> {
  let Spellings = ["__builtin_os400mi_native_bin4"];
}

def NativeBin4Set : OS400MIBuiltin<"void(void *, int)"> {
  let Spellings = ["__builtin_os400mi_native_bin4_set"];
}

def NativeBin4Get : OS400MIBuiltin<"int(void *)"> {
  let Spellings = ["__builtin_os400mi_native_bin4_get"];
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

def CharToCstr : OS400MIBuiltin<"void(char *, unsigned int, void *)"> {
  let Spellings = ["__builtin_os400mi_char_to_cstr"];
}

def Ol0 : OS400MIBuiltin<"void *()"> {
  let Spellings = ["__builtin_os400mi_ol0"];
}

def Ol1 : OS400MIBuiltin<"void *(void *)"> {
  let Spellings = ["__builtin_os400mi_ol1"];
}

def Ol2 : OS400MIBuiltin<"void *(void *, void *)"> {
  let Spellings = ["__builtin_os400mi_ol2"];
}

def Ol3 : OS400MIBuiltin<"void *(void *, void *, void *)"> {
  let Spellings = ["__builtin_os400mi_ol3"];
}

def Ol4 : OS400MIBuiltin<"void *(void *, void *, void *, void *)"> {
  let Spellings = ["__builtin_os400mi_ol4"];
}

def Ol5 : OS400MIBuiltin<"void *(void *, void *, void *, void *, void *)"> {
  let Spellings = ["__builtin_os400mi_ol5"];
}

def Ol6
    : OS400MIBuiltin<"void *(void *, void *, void *, void *, void *, void *)"> {
  let Spellings = ["__builtin_os400mi_ol6"];
}

def Ol7
    : OS400MIBuiltin<"void *(void *, void *, void *, void *, void *, void *, void *)"> {
  let Spellings = ["__builtin_os400mi_ol7"];
}

def Ol8
    : OS400MIBuiltin<"void *(void *, void *, void *, void *, void *, void *, void *, void *)"> {
  let Spellings = ["__builtin_os400mi_ol8"];
}

def Callx : OS400MIBuiltin<"void(void *, void *)"> {
  let Spellings = ["__builtin_os400mi_callx"];
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
