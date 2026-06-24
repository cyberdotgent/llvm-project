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
; CHECK:         ADDN        T000001,1,2;
; CHECK:         ADDN        T000002,T000001,536870916;
; CHECK:         ADDN        T000003,T000002,2;
; CHECK:         CPYNV       OFF,T000003;
; CHECK:              ADDSPP      .LS,.S_BASE,OFF;
; CHECK:              CPYBLA      LS_I1,X'7F';
; CHECK:         CPYNV       OFF,T000003;
; CHECK:              ADDSPP      .LS,.S_BASE,OFF;
; CHECK:              CPYBLA      U1_BOX,X'00000000';
