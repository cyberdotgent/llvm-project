; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=obj < %s | FileCheck %s

@used = global i32 7
@unused = global [300 x i8] zeroinitializer
@target = global i32 11
@ptr = global ptr @target

define i32 @main() {
entry:
  %v = load i32, ptr @used
  %p = load ptr, ptr @ptr
  %w = load i32, ptr %p
  %sum = add i32 %v, %w
  ret i32 %sum
}

; CHECK: DCL DD C_MEM CHAR(256) BDRY(16);
; CHECK: DCL DD G000001 BIN(4) DEF(C_MEM) POS(5) INIT(7);
; CHECK: DCL DD G000002 BIN(4) DEF(C_MEM) POS(9) INIT(11);
; CHECK: DCL DD G000003 CHAR(4) DEF(C_MEM) POS(13) INIT(X'10000008');
; CHECK-NOT: CHAR(300)
