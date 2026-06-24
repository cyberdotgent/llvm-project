; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=obj < %s | FileCheck %s

define i32 @main() {
entry:
  %src = alloca <4 x i8>, align 1
  %dst = alloca <4 x i8>, align 1
  %v = load <4 x i8>, ptr %src, align 1
  store <4 x i8> %v, ptr %dst, align 1
  ret i32 0
}

; CHECK: CPYBLA      U1_BYTE,LS_I1;
; CHECK: CPYBLA      LS_I1,U1_BYTE;

