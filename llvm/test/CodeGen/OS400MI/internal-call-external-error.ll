; RUN: not llc -mtriple=os400mi-ibm-os400 -filetype=obj -o - %s 2>&1 | FileCheck %s

target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"
target triple = "os400mi-ibm-os400"

declare i32 @external_fn(i32)

define i32 @main() {
entry:
  %r = call i32 @external_fn(i32 1)
  ret i32 %r
}

; CHECK: OS400MI MVP only supports defined internal callees; external calls require CALLX ABI
