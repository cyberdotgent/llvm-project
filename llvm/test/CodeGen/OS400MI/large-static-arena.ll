; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=obj < %s | FileCheck %s

@large = global [300 x i8] zeroinitializer

define i32 @main() {
entry:
  store i8 42, ptr @large
  ret i32 0
}

; CHECK: DCL DD C_MEM CHAR(304) BDRY(16);
; CHECK: DCL DD G000001 CHAR(300) DEF(C_MEM) POS(5);
