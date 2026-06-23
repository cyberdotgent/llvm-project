; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=asm < %s | FileCheck %s

target triple = "os400mi-ibm-os400"
target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"

define i32 @main() {
entry:
  %a = add i8 250, 9
  %b = xor i8 %a, 63
  %c = mul i16 300, 9
  %d = shl i16 %c, 3
  %xslot = alloca i32, align 4
  %sslot = alloca i32, align 4
  store i32 1024, ptr %xslot, align 4
  store i32 3, ptr %sslot, align 4
  %x = load i32, ptr %xslot, align 4
  %s = load i32, ptr %sslot, align 4
  %l = shl i32 %x, %s
  %r = lshr i32 %l, %s
  %e = zext i8 %b to i32
  %f = zext i16 %d to i32
  %g = add i32 %e, %f
  %h = add i32 %g, %r
  ret i32 %h
}

; CHECK: ADDN        {{T[0-9]+}},-6,9;
; CHECK: AND         {{T[0-9]+}}B,{{T[0-9]+}}B,{{T[0-9]+}}B;
; CHECK: XOR         {{T[0-9]+}}B,{{T[0-9]+}}B,{{T[0-9]+}}B;
; CHECK: MULT        {{T[0-9]+}},300,9;
; CHECK: CPYBTLLS    {{T[0-9]+}},{{T[0-9]+}},3;
; CHECK: CMPNV(B)    {{T[0-9]+}},0/EQ({{B[0-9]+}});
; CHECK: CPYBTLLS    {{T[0-9]+}},{{T[0-9]+}},1;
; CHECK: CPYBTRLS    {{T[0-9]+}},{{T[0-9]+}},1;
