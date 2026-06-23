; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=asm -o - %s | FileCheck %s

target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"
target triple = "os400mi-ibm-os400"

define i32 @main() {
  %x = add i32 1, 2
  %y = sub i32 %x, 4
  ret i32 %y
}

; CHECK: DCL     DD          T000001    BIN(4);
; CHECK: DCL     DD          T000002    BIN(4);
; CHECK: ENTRY MAIN INT;
; CHECK:         ADDN        T000001,1,2;
; CHECK:         SUBN        T000002,T000001,4;
; CHECK:         CPYNV       MAIN_RC,T000002;
; CHECK:         B           .MAIN;
