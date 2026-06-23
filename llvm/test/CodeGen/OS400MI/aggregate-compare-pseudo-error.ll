; RUN: not llc -mtriple=os400mi-ibm-os400 -filetype=obj -o - %s 2>&1 | FileCheck %s

target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"
target triple = "os400mi-ibm-os400"

declare i1 @llvm.os400mi.aggregate.eq.bad(i32, i32)

define i32 @main() {
entry:
  %bad = call i1 @llvm.os400mi.aggregate.eq.bad(i32 1, i32 1)
  %r = zext i1 %bad to i32
  ret i32 %r
}

; CHECK: OS400MI MVP only supports OS400MI aggregate equality pseudo calls with two matching supported aggregate operands

