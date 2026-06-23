; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=asm < %s | FileCheck %s

target triple = "os400mi-ibm-os400"
target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"

define i32 @main() {
entry:
  %b = alloca i8, align 1
  store i8 255, ptr %b, align 1
  %bv = load i8, ptr %b, align 1
  %sx = sext i8 %bv to i64
  %zx = zext i8 %bv to i64
  %cmp = icmp slt i64 %sx, %zx
  br i1 %cmp, label %left, label %right

left:
  br label %join

right:
  %sel = select i1 %cmp, i64 %sx, i64 %zx
  br label %join

join:
  %p = phi i64 [ %sx, %left ], [ %sel, %right ]
  %r = ashr i64 %p, 4
  %t = trunc i64 %r to i32
  ret i32 %t
}

; CHECK: CPYNV       {{T[0-9]+}},{{T[0-9]+}};
; CHECK: CMPNV(B)    {{T[0-9]+}},127/NHI({{B[0-9]+}});
; CHECK: SUBN        {{T[0-9]+}},{{T[0-9]+}},256;
; CHECK: CPYBLA      {{T[0-9]+}},X'0000000000000000';
; CHECK: CPYNV       {{T[0-9]+}}H,-1;
; CHECK: CMPNV(B)    {{T[0-9]+}}H,{{T[0-9]+}}H/LO({{B[0-9]+}});
; CHECK: CPYBLA      {{T[0-9]+}},{{T[0-9]+}};
; CHECK: CPYBLA      {{T[0-9]+}},{{T[0-9]+}};
; CHECK: CPYBTRAS    {{T[0-9]+}},{{T[0-9]+}},4;
; CHECK: CPYBLA      {{T[0-9]+}}B,{{T[0-9]+}}LB;
