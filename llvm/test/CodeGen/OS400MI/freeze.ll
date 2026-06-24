; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=obj < %s | FileCheck %s

define i32 @main() {
entry:
  %x = freeze i32 7
  ret i32 %x
}

; CHECK: CPYNV       {{T[0-9]+}},7;

