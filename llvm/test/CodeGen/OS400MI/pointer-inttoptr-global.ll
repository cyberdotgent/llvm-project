; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=obj < %s | FileCheck %s

@sentinel = global ptr inttoptr (i32 -1 to ptr)

define i32 @main() {
entry:
  %p = load ptr, ptr @sentinel
  %i = ptrtoint ptr %p to i32
  ret i32 %i
}

; CHECK: DCL DD G000001 CHAR(4) DEF(C_MEM) POS(5) INIT(X'FFFFFFFF');

