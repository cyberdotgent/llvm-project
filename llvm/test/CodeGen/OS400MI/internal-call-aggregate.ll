; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=obj -o - %s | FileCheck %s
; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=obj -o %t.mi %s
; RUN: FileCheck --check-prefix=MAP %s < %t.mi.jsonl

target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"
target triple = "os400mi-ibm-os400"

define internal { i32, i16 } @mk(i32 %x) {
entry:
  %a = insertvalue { i32, i16 } undef, i32 %x, 0
  %b = insertvalue { i32, i16 } %a, i16 7, 1
  ret { i32, i16 } %b
}

define internal i32 @sum({ i32, i16 } %p) {
entry:
  %x = extractvalue { i32, i16 } %p, 0
  %y = extractvalue { i32, i16 } %p, 1
  %zy = zext i16 %y to i32
  %r = add i32 %x, %zy
  ret i32 %r
}

define i32 @main() {
entry:
  %p = call { i32, i16 } @mk(i32 35)
  %r = call i32 @sum({ i32, i16 } %p)
  ret i32 %r
}

; CHECK: DCL     DD          F000001R    CHAR(8);
; CHECK: DCL     DD          F000001A1    BIN(4);
; CHECK: DCL     DD          F000002R    BIN(4);
; CHECK: DCL     DD          F000002A1    CHAR(8);
; CHECK: ENTRY MAIN INT;
; CHECK: CPYNV       F000001A1,35;
; CHECK: CALLI       F000001, *, .F000001;
; CHECK: CPYBLA      [[CALL_AGG:S[0-9]+]],F000001R;
; CHECK: CPYBLA      F000002A1,[[CALL_AGG]];
; CHECK: CALLI       F000002, *, .F000002;
; CHECK: CPYNV       [[SUM:T[0-9]+]],F000002R;
; CHECK: CPYNV       MAIN_RC,[[SUM]];
; CHECK: ENTRY F000001 INT;
; CHECK: CPYNV       LS_I4,F000001A1;
; CHECK: CPYNV       LS_I2,7;
; CHECK: CPYBLA      F000001R,[[RET_AGG:S[0-9]+]];
; CHECK: ENTRY F000002 INT;
; CHECK: CPYBLA      [[ARG_AGG:S[0-9]+]],F000002A1;
; CHECK: CPYNV       [[X:T[0-9]+]],LS_I4;
; CHECK: CPYNV       [[Y:T[0-9]+]],LS_I2;
; CHECK: ADDN        [[R:T[0-9]+]],[[X]],[[Y]];
; CHECK: CPYNV       F000002R,[[R]];
; CHECK: PEND;

; MAP: "mi_name":"F000001"
; MAP-SAME: "kind":"function"
; MAP-SAME: "original":"mk"
; MAP: "mi_name":"F000002"
; MAP-SAME: "kind":"function"
; MAP-SAME: "original":"sum"
; MAP-DAG: "kind":"aggregate_arg"
; MAP-DAG: "kind":"call_result"
