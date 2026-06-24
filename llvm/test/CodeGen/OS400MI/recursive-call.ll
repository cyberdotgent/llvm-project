; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=obj < %s | FileCheck %s

define i32 @main() {
entry:
  %r = call i32 @fact(i32 4)
  ret i32 %r
}

define i32 @fact(i32 %n) {
entry:
  %done = icmp ult i32 %n, 2
  br i1 %done, label %base, label %step

base:
  ret i32 1

step:
  %m = sub i32 %n, 1
  %r = call i32 @fact(i32 %m)
  %p = mul i32 %n, %r
  ret i32 %p
}

; CHECK: DCL     DD          FRAME_BASE BIN(4);
; CHECK: DCL     DD          STACK_TOP  BIN(4);
; CHECK: ADDN        STACK_TOP,STACK_TOP,
; CHECK: CALLI       F000001, *, .F000001;
; CHECK: CPYNV       STACK_TOP,
; CHECK: CPYNV       FRAME_BASE,

