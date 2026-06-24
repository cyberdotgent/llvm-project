; RUN: llc -mtriple=os400mi < %s | FileCheck %s

@lib = internal constant [9 x i8] c"LLVMWORK\00"
@pgm = internal constant [9 x i8] c"EXTUPPER\00"
@text = internal constant [4 x i8] c"abc\00"
@buf = internal global [16 x i8] zeroinitializer

define i32 @main() {
entry:
  %target = call ptr @llvm.os400mi.sysptr.program(ptr @lib, ptr @pgm)
  %in = call ptr @llvm.os400mi.native.char(i32 16)
  %out = call ptr @llvm.os400mi.native.char(i32 16)
  call void @llvm.os400mi.char.from.cstr.blank.padded(ptr %in, i32 16, ptr @text)
  %ol = call ptr @llvm.os400mi.ol.2(ptr %in, ptr %out)
  call void @llvm.os400mi.callx(ptr %target, ptr %ol)
  call void @llvm.os400mi.char.to.cstr(ptr @buf, i32 16, ptr %out)
  %bin = call ptr @llvm.os400mi.native.bin4()
  call void @llvm.os400mi.native.bin4.set(ptr %bin, i32 42)
  %got = call i32 @llvm.os400mi.native.bin4.get(ptr %bin)
  ret i32 %got
}

declare ptr @llvm.os400mi.sysptr.program(ptr, ptr)
declare ptr @llvm.os400mi.native.char(i32)
declare ptr @llvm.os400mi.ol.2(ptr, ptr)
declare void @llvm.os400mi.callx(ptr, ptr)
declare void @llvm.os400mi.char.from.cstr.blank.padded(ptr, i32, ptr)
declare void @llvm.os400mi.char.to.cstr(ptr, i32, ptr)
declare ptr @llvm.os400mi.native.bin4()
declare void @llvm.os400mi.native.bin4.set(ptr, i32)
declare i32 @llvm.os400mi.native.bin4.get(ptr)

; CHECK-DAG: DCL SYSPTR .{{[A-Z0-9]+}} INIT("EXTUPPER", CTX("LLVMWORK"), TYPE(PGM));
; CHECK-DAG: DCL     DD          {{[A-Z0-9]+}} CHAR(16);
; CHECK-DAG: DCL     SPCPTR      .NBIN4;
; CHECK-DAG: DCL     DD          NBIN4     BIN(4)     BAS(.NBIN4);
; CHECK-DAG: DCL     OL          {{[A-Z0-9]+}}(.{{[A-Z0-9]+}},.{{[A-Z0-9]+}})
; CHECK: CALLX       .{{[A-Z0-9]+}},{{[A-Z0-9]+}},*;
; CHECK: CPYBLA      LS_I1,NCHAR;
; CHECK: CPYBWP      .NBIN4,.{{[A-Z0-9]+}};
; CHECK: CPYNV       NBIN4,42;
; CHECK: CPYNV       {{[A-Z0-9]+}},NBIN4;
