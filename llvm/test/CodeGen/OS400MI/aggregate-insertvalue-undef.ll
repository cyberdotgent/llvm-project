; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=asm -o - %s | FileCheck %s

target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"
target triple = "os400mi-ibm-os400"

define i32 @main() {
entry:
  %a = insertvalue { i32, i16 } undef, i32 12, 0
  %b = insertvalue { i32, i16 } %a, i16 5, 1
  %x = extractvalue { i32, i16 } %b, 0
  %y = extractvalue { i32, i16 } %b, 1
  %zy = zext i16 %y to i32
  %sum = add i32 %x, %zy
  ret i32 %sum
}

; CHECK: CPYBLA      LS_I1,X'00';
; CHECK: CPYNV       LS_I4,12;
; CHECK: CPYNV       LS_I2,5;
; CHECK: CPYNV       MAIN_RC,{{T[0-9]+}};
