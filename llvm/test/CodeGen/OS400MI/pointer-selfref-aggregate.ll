; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=asm < %s | FileCheck %s

target triple = "os400mi-ibm-os400"
target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"

@nodes = global [4 x ptr] [ptr null, ptr @nodes, ptr getelementptr (i8, ptr @nodes, i32 4), ptr getelementptr (i8, ptr @nodes, i32 8)]

define i32 @main() {
entry:
  %p = load ptr, ptr getelementptr (i8, ptr @nodes, i32 8)
  %i = ptrtoint ptr %p to i32
  ret i32 %i
}

; CHECK: DCL DD G000001 CHAR(16) DEF(C_MEM) POS(5);
; CHECK-NEXT: DCL DD H000001 CHAR(13) DEF(G000001) POS(1) INIT(X'00000000100000041000000810');
; CHECK-NEXT: DCL DD H000002 CHAR(3) DEF(G000001) POS(14) INIT(X'00000C');
