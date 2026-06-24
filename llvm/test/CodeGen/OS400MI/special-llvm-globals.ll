; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=obj < %s | FileCheck %s

@llvm.compiler.used = appending global [1 x ptr] [ptr @helper],
  section "llvm.metadata"
@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }]
  [{ i32, ptr, ptr } { i32 65535, ptr @ctor, ptr null }]
@data = global i32 7

define internal void @helper() {
entry:
  ret void
}

define internal void @ctor() {
entry:
  ret void
}

define i32 @main() {
entry:
  %v = load i32, ptr @data
  ret i32 %v
}

; CHECK: DCL DD G000001 BIN(4)
; CHECK-NOT: llvm.compiler.used
; CHECK-NOT: llvm.global_ctors
