; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=obj < %s | FileCheck %s

define i32 @sum(i32 %n, ...) {
entry:
  %ap = alloca ptr, align 4
  call void @llvm.va_start.p0(ptr %ap)
  %a = va_arg ptr %ap, i32
  %b = va_arg ptr %ap, i32
  call void @llvm.va_end.p0(ptr %ap)
  %ab = add i32 %a, %b
  %r = add i32 %ab, %n
  ret i32 %r
}

declare void @llvm.va_start.p0(ptr)
declare void @llvm.va_end.p0(ptr)

define i32 @main() {
entry:
  %call = call i32 (i32, ...) @sum(i32 5, i32 10, i32 27)
  ret i32 %call
}

; CHECK: DCL     DD          FRAME_BASE BIN(4);
; CHECK: DCL     DD          STACK_TOP  BIN(4);
; CHECK: ENTRY MAIN INT;
; CHECK: ADDN        STACK_TOP,STACK_TOP,
; CHECK: CPYNV LS_I4,10;
; CHECK: CPYNV LS_I4,27;
; CHECK: CALLI F{{[0-9]+}}, *, .F{{[0-9]+}};
; CHECK: ENTRY F{{[0-9]+}} INT;
; CHECK: ADDN        {{T[0-9]+}},FRAME_BASE,
; CHECK: CPYNV LS_I4,{{T[0-9]+}};
; CHECK: CPYNV {{T[0-9]+}},LS_I4;
; CHECK: ADDN {{T[0-9]+}},{{T[0-9]+}},4;
; CHECK: CPYNV LS_I4,{{T[0-9]+}};
