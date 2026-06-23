; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=asm -o - %s | FileCheck %s

target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"
target triple = "os400mi-ibm-os400"

define i32 @main() {
entry:
  %x = add i32 1, 2
  %c = icmp eq i32 %x, 3
  br i1 %c, label %then, label %else

then:
  ret i32 11

else:
  ret i32 22
}

; CHECK: DCL     DD          T000001    BIN(4);
; CHECK: ENTRY MAIN INT;
; CHECK:         ADDN        T000001,1,2;
; CHECK:         CMPNV(B)    T000001,3/EQ(B000002);
; CHECK:         B           B000003;
; CHECK: B000002:
; CHECK:         CPYNV       MAIN_RC,11;
; CHECK:         B           .MAIN;
; CHECK: B000003:
; CHECK:         CPYNV       MAIN_RC,22;
; CHECK:         B           .MAIN;
