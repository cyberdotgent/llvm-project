; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=asm -o - %s | FileCheck %s

target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"
target triple = "os400mi-ibm-os400"

@src = global [4 x i8] [i8 10, i8 20, i8 30, i8 40], align 1

declare void @llvm.memcpy.p0.p0.i32(ptr noalias writeonly, ptr noalias readonly, i32, i1 immarg)
declare void @llvm.memset.p0.i32(ptr writeonly, i8, i32, i1 immarg)

define i32 @main() {
entry:
  %dst = alloca [4 x i8], align 1
  %obj = alloca { i8, i16 }, align 2
  call void @llvm.memcpy.p0.p0.i32(ptr %dst, ptr @src, i32 4, i1 false)
  store { i8, i16 } { i8 5, i16 258 }, ptr %obj, align 2
  %field = getelementptr { i8, i16 }, ptr %obj, i32 0, i32 1
  %fv = load i16, ptr %field, align 2
  call void @llvm.memset.p0.i32(ptr %dst, i8 -1, i32 2, i1 false)
  %p = getelementptr [4 x i8], ptr %dst, i32 0, i32 0
  %v = load i8, ptr %p, align 1
  %z = zext i8 %v to i32
  %zf = zext i16 %fv to i32
  %sum = add i32 %z, %zf
  ret i32 %sum
}

; CHECK: DCL DD G000001 CHAR(4) DEF(C_MEM) POS(5);
; CHECK-NEXT: DCL DD H000001 CHAR(4) DEF(G000001) POS(1) INIT(X'0A141E28');
; CHECK: DCL     DD          {{S[0-9]+}}    CHAR(4)    DEF(C_STACK)
; CHECK: DCL     DD          {{S[0-9]+}}    CHAR(4)    DEF(C_STACK)
; CHECK: CPYBLA      U1_BYTE,LS_I1;
; CHECK: CPYBLA      LS_I1,U1_BYTE;
; CHECK: CPYBLA      LS_I1,X'05';
; CHECK: CPYBLA      LS_I1,X'01';
; CHECK: CPYBLA      LS_I1,X'02';
; CHECK: CPYBLA      LS_I1,X'FF';
; CHECK: CPYNV       MAIN_RC,{{T[0-9]+}};
