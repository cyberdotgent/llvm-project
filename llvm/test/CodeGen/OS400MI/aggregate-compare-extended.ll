; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=obj -o - %s | FileCheck %s

target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"
target triple = "os400mi-ibm-os400"

@target = global i32 77, align 4
@lhs = global { [2 x i16], { ptr, i32 } } { [2 x i16] [ i16 1, i16 2 ], { ptr, i32 } { ptr @target, i32 3 } }, align 4
@rhs = global { [2 x i16], { ptr, i32 } } { [2 x i16] [ i16 1, i16 2 ], { ptr, i32 } { ptr @target, i32 3 } }, align 4

declare i1 @llvm.os400mi.aggregate.eq.nested({ [2 x i16], { ptr, i32 } }, { [2 x i16], { ptr, i32 } })
declare i1 @llvm.os400mi.aggregate.ne.nested({ [2 x i16], { ptr, i32 } }, { [2 x i16], { ptr, i32 } })

define i32 @main() {
entry:
  %a = load { [2 x i16], { ptr, i32 } }, ptr @lhs, align 4
  %same = call i1 @llvm.os400mi.aggregate.eq.nested({ [2 x i16], { ptr, i32 } } %a, { [2 x i16], { ptr, i32 } } { [2 x i16] [ i16 1, i16 2 ], { ptr, i32 } { ptr @target, i32 3 } })
  %diff = call i1 @llvm.os400mi.aggregate.ne.nested({ [2 x i16], { ptr, i32 } } %a, { [2 x i16], { ptr, i32 } } { [2 x i16] [ i16 1, i16 2 ], { ptr, i32 } { ptr @target, i32 4 } })
  %same32 = zext i1 %same to i32
  %diff32 = zext i1 %diff to i32
  %r = add i32 %same32, %diff32
  ret i32 %r
}

; CHECK: DCL DD G000002 CHAR(12) DEF(C_MEM)
; CHECK: CPYBLA      U1_BYTE,LS_I1;
; CHECK: CPYBLA      LS_I1,U1_BYTE;
; CHECK: CPYBLA      LS_I1,X'01';
; CHECK: CPYNV       {{T[0-9]+}},1;
; CHECK: CMPNV(B)    {{T[0-9]+}},{{T[0-9]+}}/NEQ({{B[0-9]+}});
; CHECK: CPYNV       {{T[0-9]+}},0;
; CHECK: CMPNV(B)    {{T[0-9]+}},{{T[0-9]+}}/NEQ({{B[0-9]+}});
; CHECK: ADDN        {{T[0-9]+}},{{T[0-9]+}},{{T[0-9]+}};
; CHECK: CPYNV       MAIN_RC,{{T[0-9]+}};
; CHECK: PEND;
