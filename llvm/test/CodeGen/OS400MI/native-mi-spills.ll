; RUN: llc -mtriple=os400mi < %s | FileCheck %s

@hello = internal constant [14 x i8] c"Hello, world!\00"
@file = internal constant [8 x i8] c"QSYSPRT\00"
@qsys = internal constant [5 x i8] c"QSYS\00"
@qdmopen = internal constant [9 x i8] c"QDMCOPEN\00"

define i32 @main() {
entry:
  %ufcb.addr = alloca ptr, align 4
  %open.addr = alloca ptr, align 4
  %ol.addr = alloca ptr, align 4
  %out.addr = alloca ptr, align 4
  %text.addr = alloca ptr, align 4

  %ufcb = call ptr @llvm.os400mi.ufcb()
  store ptr %ufcb, ptr %ufcb.addr, align 4
  %ufcb.reload = load ptr, ptr %ufcb.addr, align 4
  %filep = call ptr @llvm.os400mi.ufcb.file(ptr %ufcb.reload)
  call void @llvm.os400mi.char.from.cstr.blank.padded(ptr %filep, i32 10,
                                                      ptr @file)

  %open = call ptr @llvm.os400mi.sysptr.program(ptr @qsys, ptr @qdmopen)
  store ptr %open, ptr %open.addr, align 4
  %ufcb.arg = load ptr, ptr %ufcb.addr, align 4
  %ol = call ptr @llvm.os400mi.ol.1(ptr %ufcb.arg)
  store ptr %ol, ptr %ol.addr, align 4
  %open.reload = load ptr, ptr %open.addr, align 4
  %ol.reload = load ptr, ptr %ol.addr, align 4
  call void @llvm.os400mi.callx(ptr %open.reload, ptr %ol.reload)

  %out = call ptr @llvm.os400mi.ufcb.outbuf(ptr %ufcb.arg)
  store ptr %out, ptr %out.addr, align 4
  store ptr @hello, ptr %text.addr, align 4
  %out.reload = load ptr, ptr %out.addr, align 4
  %text.reload = load ptr, ptr %text.addr, align 4
  call void @llvm.os400mi.char.from.cstr(ptr %out.reload, i32 132,
                                         ptr %text.reload)
  ret i32 0
}

declare ptr @llvm.os400mi.ufcb()
declare ptr @llvm.os400mi.ufcb.file(ptr)
declare void @llvm.os400mi.char.from.cstr.blank.padded(ptr, i32, ptr)
declare ptr @llvm.os400mi.sysptr.program(ptr, ptr)
declare ptr @llvm.os400mi.ol.1(ptr)
declare void @llvm.os400mi.callx(ptr, ptr)
declare ptr @llvm.os400mi.ufcb.outbuf(ptr)
declare void @llvm.os400mi.char.from.cstr(ptr, i32, ptr)

; CHECK-DAG: DCL     SPCPTR      .OFCB     INIT(OFCB);
; CHECK-DAG: DCL SYSPTR .{{[A-Z0-9]+}} INIT("QDMCOPEN", CTX("QSYS"), TYPE(PGM));
; CHECK-DAG: DCL     OL          {{[A-Z0-9]+}}(.OFCB) ARG;
; CHECK: CALLX       .{{[A-Z0-9]+}},{{[A-Z0-9]+}},*;
; CHECK: CPYBLA      NCHAR,LS_I1;
