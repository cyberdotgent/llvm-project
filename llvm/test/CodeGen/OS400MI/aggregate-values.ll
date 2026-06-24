; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=asm -o - %s | FileCheck %s

target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"
target triple = "os400mi-ibm-os400"

@pair = global { i32, i16, i8 } { i32 100, i16 7, i8 1 }, align 4

define i32 @main() {
entry:
  %dst = alloca { i32, i16, i8 }, align 4
  %loaded = load { i32, i16, i8 }, ptr @pair, align 4
  %x = extractvalue { i32, i16, i8 } %loaded, 0
  %patched = insertvalue { i32, i16, i8 } %loaded, i16 9, 1
  store { i32, i16, i8 } %patched, ptr %dst, align 4
  %field = getelementptr { i32, i16, i8 }, ptr %dst, i32 0, i32 1
  %y = load i16, ptr %field, align 2
  %zy = zext i16 %y to i32
  %sum = add i32 %x, %zy
  ret i32 %sum
}

; CHECK: DCL DD G000001 CHAR(8) DEF(C_MEM) POS(5);
; CHECK-NEXT: DCL DD H000001 CHAR(8) DEF(G000001) POS(1) INIT(X'0000006400070100');
; CHECK: DCL     DD          {{S[0-9]+}}    CHAR(8)    DEF(C_MEM)
; CHECK: CPYBLA      U1_BYTE,LS_I1;
; CHECK: CPYBLA      LS_I1,U1_BYTE;
; CHECK: CPYNV       {{T[0-9]+}},LS_I4;
; CHECK: CPYNV       LS_I2,9;
; CHECK: CPYNV       MAIN_RC,{{T[0-9]+}};
