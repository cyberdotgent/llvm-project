; RUN: llc -mtriple=os400mi < %s | FileCheck %s --implicit-check-not='.SEPT('
; RUN: rm -f %t.mi %t.mi.jsonl
; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=asm -o %t.mi %s
; RUN: FileCheck --check-prefix=MAP --input-file=%t.mi.jsonl %s

@hello = internal constant [14 x i8] c"Hello, world!\00"
@file = internal constant [8 x i8] c"QSYSPRT\00"
@library = internal constant [6 x i8] c"*LIBL\00"
@member = internal constant [7 x i8] c"*FIRST\00"
@qsys = internal constant [5 x i8] c"QSYS\00"
@qdmopen = internal constant [9 x i8] c"QDMCOPEN\00"
@qdmclose = internal constant [9 x i8] c"QDMCLOSE\00"

define i32 @main() {
entry:
  %ufcb = call ptr @llvm.os400mi.ufcb()
  %filep = call ptr @llvm.os400mi.ufcb.file(ptr %ufcb)
  call void @llvm.os400mi.char.from.cstr.blank.padded(ptr %filep, i32 10, ptr @file)
  %libraryp = call ptr @llvm.os400mi.ufcb.library(ptr %ufcb)
  call void @llvm.os400mi.char.from.cstr.blank.padded(ptr %libraryp, i32 10, ptr @library)
  %memberp = call ptr @llvm.os400mi.ufcb.member(ptr %ufcb)
  call void @llvm.os400mi.char.from.cstr.blank.padded(ptr %memberp, i32 10, ptr @member)
  %open = call ptr @llvm.os400mi.sysptr.program(ptr @qsys, ptr @qdmopen)
  call void @llvm.os400mi.callx.1(ptr %open, ptr %ufcb)
  %out = call ptr @llvm.os400mi.ufcb.outbuf(ptr %ufcb)
  call void @llvm.os400mi.char.fill(ptr %out, i32 132, i32 32)
  call void @llvm.os400mi.char.from.cstr(ptr %out, i32 132, ptr @hello)
  %odp = call ptr @llvm.os400mi.ufcb.odp(ptr %ufcb)
  %putent = call i16 @llvm.os400mi.odp.dcb.put(ptr %odp)
  %put = call ptr @llvm.os400mi.sysptr.sept(i16 %putent)
  %opt = call ptr @llvm.os400mi.dm.put.wait.option()
  %null = call ptr @llvm.os400mi.spcptr.null()
  call void @llvm.os400mi.callx.3(ptr %put, ptr %ufcb, ptr %opt, ptr %null)
  %close = call ptr @llvm.os400mi.sysptr.program(ptr @qsys, ptr @qdmclose)
  call void @llvm.os400mi.callx.1(ptr %close, ptr %ufcb)
  ret i32 0
}

declare ptr @llvm.os400mi.ufcb()
declare ptr @llvm.os400mi.ufcb.file(ptr)
declare ptr @llvm.os400mi.ufcb.library(ptr)
declare ptr @llvm.os400mi.ufcb.member(ptr)
declare ptr @llvm.os400mi.sysptr.sept(i16)
declare ptr @llvm.os400mi.sysptr.program(ptr, ptr)
declare void @llvm.os400mi.callx.1(ptr, ptr)
declare ptr @llvm.os400mi.ufcb.outbuf(ptr)
declare void @llvm.os400mi.char.fill(ptr, i32, i32)
declare void @llvm.os400mi.char.from.cstr(ptr, i32, ptr)
declare void @llvm.os400mi.char.from.cstr.blank.padded(ptr, i32, ptr)
declare ptr @llvm.os400mi.ufcb.odp(ptr)
declare i16 @llvm.os400mi.odp.dcb.put(ptr)
declare ptr @llvm.os400mi.dm.put.wait.option()
declare ptr @llvm.os400mi.spcptr.null()
declare void @llvm.os400mi.callx.3(ptr, ptr, ptr, ptr)

; CHECK-DAG: DCL     SPCPTR      @SEPT     BASPCO;
; CHECK-DAG: DCL     SPCPTR      .{{[A-Z0-9]+}};
; CHECK-DAG: DCL     SYSPTR      .{{[A-Z0-9]+}} BAS(.{{[A-Z0-9]+}});
; CHECK-DAG: DCL     SPCPTR      .OFCB     INIT(OFCB);
; CHECK-DAG: DCL SYSPTR .{{[A-Z0-9]+}} INIT("QDMCOPEN", CTX("QSYS"), TYPE(PGM));
; CHECK-DAG: DCL SYSPTR .{{[A-Z0-9]+}} INIT("QDMCLOSE", CTX("QSYS"), TYPE(PGM));
; CHECK-DAG: DCL     DD          OFCB-FILE    CHAR(10) DEF(OFCB) POS(129);
; CHECK-DAG: DCL     DD          OFCB-LIBRARY CHAR(10) DEF(OFCB) POS(141);
; CHECK-DAG: DCL     DD          OFCB-MEMBER  CHAR(10) DEF(OFCB) POS(153);
; CHECK-DAG: DCL     DD          OUTBUF    CHAR(132) BAS(.OFCB-OUTBUF);
; CHECK-DAG: DCL     DD          NCHAR     CHAR(1)    BAS(.NCHAR);
; CHECK-NOT: INIT("QSYSPRT")
; CHECK-NOT: INIT("*LIBL")
; CHECK-NOT: INIT("*FIRST")
; CHECK: CALLX       .{{[A-Z0-9]+}},{{[^,]+}},*;
; CHECK: CPYBREP     OUTBUF," ";
; CHECK: CMPBLA(B)   LS_I1,X'00'/EQ(
; CHECK: CPYBLA      NCHAR,LS_I1;
; CHECK: CPYNV       {{[A-Z0-9]+}},PUT-ENTRY;
; CHECK: SUBN        {{[A-Z0-9]+}},{{[A-Z0-9]+}},1;
; CHECK: MULT        {{[A-Z0-9]+}},{{[A-Z0-9]+}},16;
; CHECK: ADDSPP      .{{[A-Z0-9]+}},@SEPT,{{[A-Z0-9]+}};
; CHECK: CALLX       .{{[A-Z0-9]+}},{{[^,]+}},*;
; CHECK: CALLX       .{{[A-Z0-9]+}},{{[^,]+}},*;

; MAP-DAG: {"mi_name":".{{[A-Z0-9]+}}","kind":"native_sept_entry_spcptr","name_class":"temp","name_ordinal":{{[0-9]+}},"max_name_length":48,"collision":false,"hash":"{{[0-9A-F]+}}","original":"put","size":16,"alignment":16}
; MAP-DAG: {"mi_name":".{{[A-Z0-9]+}}","kind":"native_sept_entry_sysptr","name_class":"temp","name_ordinal":{{[0-9]+}},"max_name_length":48,"collision":false,"hash":"{{[0-9A-F]+}}","original":"put","size":16,"alignment":16}
; MAP-DAG: {"mi_name":"{{[A-Z0-9]+}}","kind":"native_sept_entry_offset","name_class":"temp","name_ordinal":{{[0-9]+}},"max_name_length":48,"collision":false,"hash":"{{[0-9A-F]+}}","original":"put","size":4,"alignment":4}
