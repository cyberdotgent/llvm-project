; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=obj < %s | FileCheck %s

define i32 @main() {
entry:
  %a = trunc i32 291 to i8
  %b = zext i8 %a to i32
  ret i32 %b
}

; CHECK: CPYNV       {{T[0-9]+}},291;
; CHECK: AND

