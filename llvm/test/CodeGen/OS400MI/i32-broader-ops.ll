; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=asm -o - %s | FileCheck %s

target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"
target triple = "os400mi-ibm-os400"

define i32 @main() {
entry:
  %a.addr = alloca i32
  %b.addr = alloca i32
  store i32 13, ptr %a.addr
  store i32 3, ptr %b.addr
  %a = load i32, ptr %a.addr
  %b = load i32, ptr %b.addr
  %mul = mul i32 %a, %b
  %div = sdiv i32 %mul, %b
  %rem = srem i32 %mul, %a
  %and = and i32 %div, 255
  %or = or i32 %and, %rem
  %xor = xor i32 %or, %b
  %shl = shl i32 %xor, 1
  %lshr = lshr i32 %shl, 1
  %ashr = ashr i32 %lshr, 1
  ret i32 %ashr
}

; CHECK: MULT        {{T[0-9]+}},{{T[0-9]+}},{{T[0-9]+}};
; CHECK: DIV         {{T[0-9]+}},{{T[0-9]+}},{{T[0-9]+}};
; CHECK: REM         {{T[0-9]+}},{{T[0-9]+}},{{T[0-9]+}};
; CHECK: AND         {{T[0-9]+}}B,{{T[0-9]+}}B,{{T[0-9]+}}B;
; CHECK: OR          {{T[0-9]+}}B,{{T[0-9]+}}B,{{T[0-9]+}}B;
; CHECK: XOR         {{T[0-9]+}}B,{{T[0-9]+}}B,{{T[0-9]+}}B;
; CHECK: CPYBTLLS    {{T[0-9]+}},{{T[0-9]+}},1;
; CHECK: CPYBTRLS    {{T[0-9]+}},{{T[0-9]+}},1;
; CHECK: CPYBTRAS    {{T[0-9]+}},{{T[0-9]+}},1;
; CHECK: CPYNV       MAIN_RC,{{T[0-9]+}};
