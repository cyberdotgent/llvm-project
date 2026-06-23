; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=asm < %s | FileCheck %s

target triple = "os400mi-ibm-os400"
target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"

declare void @llvm.memcpy.p0.p0.i32(ptr nocapture writeonly, ptr nocapture readonly, i32, i1 immarg)
declare void @llvm.memset.p0.i32(ptr nocapture writeonly, i8, i32, i1 immarg)

define i32 @main() {
entry:
  %src = alloca [16 x i8], align 4
  %dst = alloca [16 x i8], align 4
  %pairs = alloca [4 x { i32, i32 }], align 4
  %nslot = alloca i32, align 4
  store i32 5, ptr %nslot, align 4
  %n = load i32, ptr %nslot, align 4
  call void @llvm.memset.p0.i32(ptr %src, i8 65, i32 %n, i1 false)
  call void @llvm.memcpy.p0.p0.i32(ptr %dst, ptr %src, i32 %n, i1 false)
  %elt = getelementptr [4 x { i32, i32 }], ptr %pairs, i32 0, i32 %n, i32 1
  store i32 99, ptr %elt, align 4
  %v = load i32, ptr %elt, align 4
  ret i32 %v
}

; CHECK: CMPNV(B)    {{T[0-9]+}},0/EQ({{B[0-9]+}});
; CHECK: CPYBLA      LS_I1,X'41';
; CHECK: CPYBLA      {{U1_BYTE|LS_I1}},{{LS_I1|U1_BYTE}};
; CHECK: ADDN        {{T[0-9]+}},{{T[0-9]+}},1;
; CHECK: SUBN        {{T[0-9]+}},{{T[0-9]+}},1;
; CHECK: MULT        {{T[0-9]+}},{{T[0-9]+}},8;
