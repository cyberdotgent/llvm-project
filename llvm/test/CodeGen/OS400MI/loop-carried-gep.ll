; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=obj < %s | FileCheck %s

@str = private constant [3 x i8] c"ab\00"

define i32 @main() {
entry:
  br label %next

body:
  %c = load i8, ptr %p.next
  %done = icmp eq i8 %c, 0
  br i1 %done, label %exit, label %next

next:
  %p = phi ptr [ @str, %entry ], [ %p.next, %body ]
  %p.next = getelementptr i8, ptr %p, i32 1
  br label %body

exit:
  ret i32 0
}

; CHECK: ADDN
; CHECK: CPYNV
