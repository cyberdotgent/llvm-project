; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=asm -o - %s | FileCheck %s

target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"
target triple = "os400mi-ibm-os400"

define i32 @main() {
entry:
  %cmp = icmp eq i32 4, 4
  %b = zext i1 %cmp to i32
  %sel = select i1 %cmp, i32 7, i32 9
  %sum = add i32 %b, %sel
  ret i32 %sum
}

; CHECK: ENTRY MAIN INT;
; CHECK: B000001:
; CHECK-NEXT:         CPYNV       T000001,0;
; CHECK-NEXT:         CMPNV(B)    4,4/EQ(B000002);
; CHECK-NEXT:         B           B000003;
; CHECK-NEXT: B000002:
; CHECK-NEXT:         CPYNV       T000001,1;
; CHECK-NEXT: B000003:
; CHECK-NEXT:         CPYNV       T000002,9;
; CHECK-NEXT:         CMPNV(B)    4,4/EQ(B000004);
; CHECK-NEXT:         B           B000005;
; CHECK-NEXT: B000004:
; CHECK-NEXT:         CPYNV       T000002,7;
; CHECK-NEXT: B000005:
; CHECK-NEXT:         ADDN        T000003,T000001,T000002;
; CHECK-NEXT:         CPYNV       MAIN_RC,T000003;

