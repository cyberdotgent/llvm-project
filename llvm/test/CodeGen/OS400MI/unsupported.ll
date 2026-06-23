; RUN: not llc -mtriple=os400mi-ibm-os400 -filetype=obj -o - %s 2>&1 | FileCheck %s

target datalayout = "E-p:32:32-i8:32:32-i16:32:32-i32:32:32-i64:32:32-n32-S32"
target triple = "os400mi-ibm-os400"

define i32 @main() {
  %x = add i32 1, 2
  ret i32 %x
}

; CHECK: OS400MI MVP only supports main with a single ret instruction
