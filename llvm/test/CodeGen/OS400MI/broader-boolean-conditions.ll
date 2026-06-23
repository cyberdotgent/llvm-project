; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=asm -o - %s | FileCheck %s

target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"
target triple = "os400mi-ibm-os400"

define i32 @main() {
entry:
  %bytes = alloca [2 x i8]
  %bp = getelementptr [2 x i8], ptr %bytes, i32 0, i32 0
  store i8 200, ptr %bp
  %bv = load i8, ptr %bp
  %narrow = icmp ugt i8 %bv, 127
  br i1 %narrow, label %pick, label %low

pick:
  %same = icmp eq i32 4, 4
  %cond = select i1 %same, i1 true, i1 false
  br i1 %cond, label %left, label %right

left:
  br label %join

right:
  br label %join

low:
  br label %join

join:
  %flag = phi i1 [ true, %left ], [ false, %right ], [ false, %low ]
  %result = select i1 %flag, i32 11, i32 22
  ret i32 %result
}

; CHECK: CPYNV       {{T[0-9]+}},U1_NUM;
; CHECK: CMPNV(B)    {{T[0-9]+}},127/HI(
; CHECK: CPYNV       {{T[0-9]+}},0;
; CHECK: CMPNV(B)    4,4/EQ(
; CHECK: CPYNV       {{T[0-9]+}},1;
; CHECK: CMPNV(B)    {{T[0-9]+}},0/NEQ(
; CHECK: CPYNV       {{T[0-9]+}},22;
; CHECK: CMPNV(B)    {{T[0-9]+}},0/NEQ(
; CHECK: CPYNV       {{T[0-9]+}},11;
; CHECK: CPYNV       MAIN_RC,{{T[0-9]+}};
