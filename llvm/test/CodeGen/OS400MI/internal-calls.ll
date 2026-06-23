; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=obj -o - %s | FileCheck %s
; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=obj -o %t.mi %s
; RUN: FileCheck --check-prefix=MAP %s < %t.mi.jsonl

target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"
target triple = "os400mi-ibm-os400"

define internal i32 @add1(i32 %x) {
entry:
  %y = add i32 %x, 1
  ret i32 %y
}

define internal i32 @sum2(i32 %a, i32 %b) {
entry:
  %r = add i32 %a, %b
  ret i32 %r
}

define i32 @main() {
entry:
  %one = call i32 @add1(i32 40)
  %two = call i32 @sum2(i32 %one, i32 1)
  ret i32 %two
}

; CHECK: DCL     INSPTR      .F000001;
; CHECK: DCL     DD          F000001R    BIN(4);
; CHECK: DCL     DD          F000001A1    BIN(4);
; CHECK: DCL     INSPTR      .F000002;
; CHECK: DCL     DD          F000002R    BIN(4);
; CHECK: DCL     DD          F000002A1    BIN(4);
; CHECK: DCL     DD          F000002A2    BIN(4);
; CHECK: ENTRY MAIN INT;
; CHECK: CPYNV       F000001A1,40;
; CHECK: CALLI       F000001, *, .F000001;
; CHECK: CPYNV       [[CALL1:T[0-9]+]],F000001R;
; CHECK: CPYNV       F000002A1,[[CALL1]];
; CHECK: CPYNV       F000002A2,1;
; CHECK: CALLI       F000002, *, .F000002;
; CHECK: CPYNV       [[CALL2:T[0-9]+]],F000002R;
; CHECK: CPYNV       MAIN_RC,[[CALL2]];
; CHECK: B           .MAIN;
; CHECK: ENTRY F000001 INT;
; CHECK: ADDN        [[ADD1:T[0-9]+]],F000001A1,1;
; CHECK: CPYNV       F000001R,[[ADD1]];
; CHECK: B           .F000001;
; CHECK: ENTRY F000002 INT;
; CHECK: ADDN        [[SUM:T[0-9]+]],F000002A1,F000002A2;
; CHECK: CPYNV       F000002R,[[SUM]];
; CHECK: B           .F000002;
; CHECK: PEND;

; MAP: "mi_name":"MAIN"
; MAP-SAME: "kind":"function"
; MAP-SAME: "original":"main"
; MAP: "mi_name":"F000001"
; MAP-SAME: "kind":"function"
; MAP-SAME: "name_class":"function"
; MAP-SAME: "original":"add1"
; MAP: "mi_name":"F000002"
; MAP-SAME: "kind":"function"
; MAP-SAME: "name_class":"function"
; MAP-SAME: "original":"sum2"
