; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=obj < %s | FileCheck %s

define i32 @first_twice(i32 %tag, ...) {
entry:
  %ap = alloca ptr, align 4
  %copy = alloca ptr, align 4
  call void @llvm.va_start.p0(ptr %ap)
  call void @llvm.va_copy.p0.p0(ptr %copy, ptr %ap)
  %a = va_arg ptr %ap, i32
  %b = va_arg ptr %copy, i32
  call void @llvm.va_end.p0(ptr %copy)
  call void @llvm.va_end.p0(ptr %ap)
  %sum = add i32 %a, %b
  %r = add i32 %sum, %tag
  ret i32 %r
}

declare void @llvm.va_start.p0(ptr)
declare void @llvm.va_copy.p0.p0(ptr, ptr)
declare void @llvm.va_end.p0(ptr)

define i32 @main() {
entry:
  %call = call i32 (i32, ...) @first_twice(i32 1, i32 20)
  ret i32 %call
}

; CHECK: DCL DD S{{[0-9]+}} CHAR(4) DEF(C_STACK) POS(5);
; CHECK: CPYNV LS_I4,20;
; CHECK: CPYNV LS_I4,536870916;
; CHECK: CPYNV {{T[0-9]+}},LS_I4;
; CHECK: CPYNV LS_I4,{{T[0-9]+}};
; CHECK: CPYNV {{T[0-9]+}},LS_I4;
