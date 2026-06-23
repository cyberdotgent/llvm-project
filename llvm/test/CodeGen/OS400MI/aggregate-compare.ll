; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=obj -o - %s | FileCheck %s
; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=obj -o %t.mi %s
; RUN: FileCheck --check-prefix=MAP %s < %t.mi.jsonl

target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"
target triple = "os400mi-ibm-os400"

@same1 = global { i32, i16 } { i32 11, i16 22 }, align 4
@same2 = global { i32, i16 } { i32 11, i16 22 }, align 4

declare i1 @llvm.os400mi.aggregate.eq.pair({ i32, i16 }, { i32, i16 })
declare i1 @llvm.os400mi.aggregate.ne.pair({ i32, i16 }, { i32, i16 })

define internal { i32, i16 } @mk(i32 %x, i16 %y) {
entry:
  %a = insertvalue { i32, i16 } undef, i32 %x, 0
  %b = insertvalue { i32, i16 } %a, i16 %y, 1
  ret { i32, i16 } %b
}

define i32 @main() {
entry:
  %a = load { i32, i16 }, ptr @same1, align 4
  %b = load { i32, i16 }, ptr @same2, align 4
  %eq = call i1 @llvm.os400mi.aggregate.eq.pair({ i32, i16 } %a, { i32, i16 } %b)
  br i1 %eq, label %matched, label %failed

matched:
  %c = call { i32, i16 } @mk(i32 11, i16 22)
  %d = call { i32, i16 } @mk(i32 11, i16 23)
  %ne = call i1 @llvm.os400mi.aggregate.ne.pair({ i32, i16 } %c, { i32, i16 } %d)
  %r = zext i1 %ne to i32
  ret i32 %r

failed:
  ret i32 99
}

; CHECK: ENTRY MAIN INT;
; CHECK: CPYBLA      U1_BYTE,LS_I1;
; CHECK: CPYBLA      LS_I1,U1_BYTE;
; CHECK: CPYNV       [[EQ:T[0-9]+]],1;
; CHECK: CMPNV(B)    {{T[0-9]+}},{{T[0-9]+}}/NEQ([[EQ_FALSE:B[0-9]+]]);
; CHECK: [[EQ_FALSE]]:
; CHECK: CPYNV       [[EQ]],0;
; CHECK: CMPNV(B)    [[EQ]],0/NEQ({{B[0-9]+}});
; CHECK: CALLI       F000001, *, .F000001;
; CHECK: CALLI       F000001, *, .F000001;
; CHECK: CPYNV       [[NE:T[0-9]+]],0;
; CHECK: CMPNV(B)    {{T[0-9]+}},{{T[0-9]+}}/NEQ([[NE_TRUE:B[0-9]+]]);
; CHECK: [[NE_TRUE]]:
; CHECK: CPYNV       [[NE]],1;
; CHECK: CPYNV       MAIN_RC,[[NE]];
; CHECK: PEND;

; MAP-DAG: "kind":"aggregate_compare_false"
; MAP-DAG: "kind":"aggregate_compare_true"
; MAP-DAG: "kind":"aggregate_compare_done"
