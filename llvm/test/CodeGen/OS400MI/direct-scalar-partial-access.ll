; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=obj < %s | FileCheck %s

@guard = global i32 0

define void @init_guard() {
entry:
  store i8 0, ptr @guard
  store i8 10, ptr getelementptr (i8, ptr @guard, i32 2)
  ret void
}

define i32 @main() {
entry:
  call void @init_guard()
  %byte = load i8, ptr getelementptr (i8, ptr @guard, i32 2)
  %wide = zext i8 %byte to i32
  ret i32 %wide
}

; CHECK: DCL DD G000001 BIN(4) DEF(C_MEM) POS(5) INIT(0);
; CHECK: ADDSPP      .LS,.C_BASE,OFF;
; CHECK: CPYBLA      U1_BYTE,LS_I1;
; CHECK: CPYNV       {{T[0-9]+}},U1_NUM;
; CHECK-DAG: CPYBLA      LS_I1,X'00';
; CHECK-DAG: CPYBLA      LS_I1,X'0A';
