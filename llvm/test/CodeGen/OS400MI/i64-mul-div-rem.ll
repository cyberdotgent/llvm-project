; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=asm < %s | FileCheck %s

target triple = "os400mi-ibm-os400"
target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"

define i32 @main() {
entry:
  %slot = alloca i64, align 4
  store i64 123456789, ptr %slot, align 4
  %x = load i64, ptr %slot, align 4
  %m = mul i64 %x, 97
  %u = udiv i64 %m, 17
  %ur = urem i64 %m, 17
  %s = sdiv i64 %m, -13
  %sr = srem i64 %m, -13
  %a = add i64 %u, %ur
  %b = add i64 %s, %sr
  %c = add i64 %a, %b
  %t = trunc i64 %c to i32
  ret i32 %t
}

; CHECK: CPYNV       {{T[0-9]+}},64;
; CHECK: AND         {{T[0-9]+}}B,{{T[0-9]+}}LB,{{T[0-9]+}}B;
; CHECK: XOR        {{T[0-9]+}},{{T[0-9]+}},{{T[0-9]+}};
; CHECK: AND        {{T[0-9]+}},{{T[0-9]+}},{{T[0-9]+}};
; CHECK: CPYBTRLS    {{T[0-9]+}},{{T[0-9]+}},1;
; CHECK: CMPNV(B)    {{T[0-9]+}}U,{{T[0-9]+}}U/HI({{B[0-9]+}});
; CHECK: CPYBLA      {{T[0-9]+}},X'FFFFFFFFFFFFFFFF';
; CHECK: XOR        {{T[0-9]+}},{{T[0-9]+}},{{T[0-9]+}};
; CHECK: CMPNV(B)    {{T[0-9]+}}H,0/NLO({{B[0-9]+}});
; CHECK: CPYBLA      {{T[0-9]+}},X'FFFFFFFFFFFFFFF3';
; CHECK: CPYNV       {{T[0-9]+}},1;
