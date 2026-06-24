; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=obj < %s | FileCheck %s

declare void @llvm.os400mi.runtime.startup()
declare i32 @llvm.os400mi.runtime.terminate(i32)

@state = global i32 0
@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }]
  [{ i32, ptr, ptr } { i32 10, ptr @ctor, ptr null }]
@llvm.global_dtors = appending global [1 x { i32, ptr, ptr }]
  [{ i32, ptr, ptr } { i32 10, ptr @dtor, ptr null }]

define i32 @_start() {
entry:
  call void @llvm.os400mi.runtime.startup()
  %rc = call i32 @main()
  %done = call i32 @llvm.os400mi.runtime.terminate(i32 %rc)
  ret i32 %done
}

define i32 @main() {
entry:
  %v = load i32, ptr @state
  ret i32 %v
}

define internal void @ctor() {
entry:
  store i32 42, ptr @state
  ret void
}

define internal void @dtor() {
entry:
  store i32 0, ptr @state
  ret void
}

; CHECK: DCL DD G000001 BIN(4) DEF(C_MEM) POS(5) INIT(0);
; CHECK-NOT: llvm.global_ctors
; CHECK-NOT: llvm.global_dtors
; CHECK: ENTRY MAIN INT;
; CHECK: CALLI F000002, *, .F000002;
; CHECK: CALLI F000001, *, .F000001;
; CHECK: CALLI F000003, *, .F000003;
