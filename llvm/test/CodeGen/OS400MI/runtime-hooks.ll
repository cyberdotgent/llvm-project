; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=obj -o - %s | FileCheck %s

target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"
target triple = "os400mi-ibm-os400"

declare void @llvm.os400mi.runtime.startup()
declare i32 @llvm.os400mi.runtime.terminate(i32)

define internal void @touch_runtime_state() {
entry:
  ret void
}

define internal i32 @main() {
entry:
  ret i32 42
}

define i32 @_start() {
entry:
  call void @llvm.os400mi.runtime.startup()
  call void @touch_runtime_state()
  %rc = call i32 @main()
  %done = call i32 @llvm.os400mi.runtime.terminate(i32 %rc)
  ret i32 %done
}

; CHECK: ENTRY * (PARM_LIST) EXT;
; CHECK: CALLI       MAIN, *, .MAIN;
; CHECK: ENTRY MAIN INT;
; CHECK-NOT: runtime
; CHECK: CALLI       F000001, *, .F000001;
; CHECK: CALLI       F000002, *, .F000002;
; CHECK: CPYNV       MAIN_RC,{{T[0-9]+}};
; CHECK: B           .MAIN;
; CHECK: ENTRY F000001 INT;
; CHECK-NOT: F000001R
; CHECK: B           .F000001;
; CHECK: ENTRY F000002 INT;
