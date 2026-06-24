; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=obj < %s | FileCheck %s

define i32 @main() {
entry:
  %cond = icmp eq i32 1, 0
  br i1 %cond, label %left, label %right

left:
  br label %done

right:
  br label %done

done:
  %p = phi i8 [ 7, %left ], [ 9, %right ]
  %z = zext i8 %p to i32
  ret i32 %z
}

; CHECK: CPYNV
; CHECK: CPYNV
; CHECK: CPYNV       MAIN_RC
