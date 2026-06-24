; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=obj < %s | FileCheck %s

define i32 @main() {
entry:
  %a = icmp eq i32 1, 1
  %b = icmp eq i32 2, 3
  %c = or i1 %a, %b
  %z = zext i1 %c to i32
  ret i32 %z
}

; CHECK: OR          {{T[0-9]+}}B,
; CHECK: AND

