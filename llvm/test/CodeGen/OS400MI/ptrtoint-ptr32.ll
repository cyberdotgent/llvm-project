; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=obj < %s | FileCheck %s

@str = private constant [4 x i8] c"abc\00"

define i32 @main() {
entry:
  %p = ptrtoint ptr @str to i32
  %low = and i32 %p, 3
  ret i32 %low
}

; CHECK: CPYNV {{.*}},268435460
; CHECK: AND
