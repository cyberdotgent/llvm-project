; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=asm < %s | FileCheck %s

target triple = "os400mi-ibm-os400"
target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"

define i32 @main() {
entry:
  %slot = alloca i64, align 4
  store i64 81985529216486895, ptr %slot, align 4
  %x = load i64, ptr %slot, align 4
  %a = and i64 %x, -71777214294589696
  %o = or i64 %a, 255
  %y = xor i64 %o, 1085102592571150095
  %amt = zext i32 3 to i64
  %l = shl i64 %y, %amt
  %r = lshr i64 %l, %amt
  %s = ashr i64 %r, %amt
  %t = trunc i64 %s to i32
  ret i32 %t
}

; CHECK: AND        {{T[0-9]+}},{{T[0-9]+}},{{T[0-9]+}};
; CHECK: OR         {{T[0-9]+}},{{T[0-9]+}},{{T[0-9]+}};
; CHECK: XOR        {{T[0-9]+}},{{T[0-9]+}},{{T[0-9]+}};
; CHECK: CPYNV       {{T[0-9]+}},{{T[0-9]+}}L;
; CHECK: CMPNV(B)    {{T[0-9]+}},0/EQ({{B[0-9]+}});
; CHECK: CPYBTLLS    {{T[0-9]+}},{{T[0-9]+}},1;
; CHECK: CPYBTRLS    {{T[0-9]+}},{{T[0-9]+}},1;
; CHECK: CPYBTRAS    {{T[0-9]+}},{{T[0-9]+}},1;

