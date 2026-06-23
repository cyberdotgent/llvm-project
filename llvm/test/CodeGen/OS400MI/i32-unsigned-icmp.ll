; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=asm -o - %s | FileCheck %s

target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"
target triple = "os400mi-ibm-os400"

define i32 @main() {
entry:
  %ugt = icmp ugt i32 5, 3
  %a = zext i1 %ugt to i32
  %ule = icmp ule i32 5, 3
  %b = zext i1 %ule to i32
  %sum = add i32 %a, %b
  ret i32 %sum
}

; CHECK:         CMPNV(B)    5,3/HI(B000002);
; CHECK:         CMPNV(B)    5,3/NHI(B000004);

