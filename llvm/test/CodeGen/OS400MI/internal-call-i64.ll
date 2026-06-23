; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=obj -o - %s | FileCheck %s

target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"
target triple = "os400mi-ibm-os400"

define internal i64 @add64(i64 %x) {
entry:
  %y = add i64 %x, 5
  ret i64 %y
}

define internal i64 @sum64(i64 %a, i64 %b) {
entry:
  %r = add i64 %a, %b
  ret i64 %r
}

define i32 @main() {
entry:
  %one = call i64 @add64(i64 37)
  %two = call i64 @sum64(i64 %one, i64 0)
  %r = trunc i64 %two to i32
  ret i32 %r
}

; CHECK: DCL     DD          F000001R    CHAR(8);
; CHECK: DCL     DD          F000001A1    CHAR(8);
; CHECK: DCL     DD          F000002R    CHAR(8);
; CHECK: DCL     DD          F000002A1    CHAR(8);
; CHECK: DCL     DD          F000002A2    CHAR(8);
; CHECK: ENTRY MAIN INT;
; CHECK: CPYBLA      F000001A1,[[ARG:T[0-9]+]];
; CHECK: CALLI       F000001, *, .F000001;
; CHECK: CPYBLA      [[ONE:T[0-9]+]],F000001R;
; CHECK: CPYBLA      F000002A1,[[ONE]];
; CHECK: CPYBLA      F000002A2,[[ZERO:T[0-9]+]];
; CHECK: CALLI       F000002, *, .F000002;
; CHECK: CPYBLA      [[TWO:T[0-9]+]],F000002R;
; CHECK: CPYBLA      [[TRUNC:T[0-9]+]]B,[[TWO]]LB;
; CHECK: CPYNV       MAIN_RC,[[TRUNC]];
; CHECK: ENTRY F000001 INT;
; CHECK: CPYBLA      [[ADD1:T[0-9]+]],F000001A1;
; CHECK: CPYBLA      F000001R,[[ADD1]];
; CHECK: ENTRY F000002 INT;
; CHECK: CPYBLA      [[SUML:T[0-9]+]],F000002A1;
; CHECK: CPYBLA      [[SUMR:T[0-9]+]],F000002A2;
; CHECK: CPYBLA      F000002R,[[SUML]];
; CHECK: PEND;
