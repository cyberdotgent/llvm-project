; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=asm -o - %s | FileCheck %s

target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"
target triple = "os400mi-ibm-os400"

define i32 @main() {
entry:
  %b = alloca i8
  %s = alloca i16
  store i8 -2, ptr %b
  store i16 -3, ptr %s
  %bv = load i8, ptr %b
  %sv = load i16, ptr %s
  %bc = icmp slt i8 %bv, 0
  %sc = icmp sgt i16 %sv, -4
  %bi = zext i1 %bc to i32
  %si = zext i1 %sc to i32
  %sum = add i32 %bi, %si
  ret i32 %sum
}

; CHECK: CPYBLA      {{S[0-9]+}},X'FE';
; CHECK: CPYNV       {{S[0-9]+}},65533;
; CHECK: SUBN        {{T[0-9]+}},{{T[0-9]+}},256;
; CHECK: SUBN        {{T[0-9]+}},{{T[0-9]+}},65536;
; CHECK: CMPNV(B)    {{T[0-9]+}},0/LO(
; CHECK: CMPNV(B)    {{T[0-9]+}},-4/HI(
; CHECK: ADDN        {{T[0-9]+}},{{T[0-9]+}},{{T[0-9]+}};
; CHECK: CPYNV       MAIN_RC,{{T[0-9]+}};
