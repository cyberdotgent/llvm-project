; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=asm -o - %s | FileCheck %s

target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"
target triple = "os400mi-ibm-os400"

define i32 @main() {
entry:
  %arr = alloca [8 x i8]
  %idx = add i32 1, 2
  %p = getelementptr [8 x i8], ptr %arr, i32 0, i32 %idx
  %q = getelementptr i8, ptr %p, i32 2
  store i8 127, ptr %q
  %v = load i8, ptr %q
  %r = zext i8 %v to i32
  ret i32 %r
}

; CHECK: DCL     DD          S000001    CHAR(8)    DEF(C_STACK) POS(5);
; CHECK:         ADDN        [[IDX:T[0-9]+]],1,2;
; CHECK:         ADDN        [[P:T[0-9]+]],[[IDX]],536870916;
; CHECK:         ADDN        [[Q:T[0-9]+]],[[P]],2;
; CHECK:         CPYNV       OFF,[[Q]];
; CHECK:              ADDSPP      .LS,.S_BASE,OFF;
; CHECK:              CPYBLA      LS_I1,X'7F';
; CHECK:         CPYNV       OFF,[[Q]];
; CHECK:              ADDSPP      .LS,.S_BASE,OFF;
; CHECK:              CPYBLA      U1_BOX,X'00000000';
