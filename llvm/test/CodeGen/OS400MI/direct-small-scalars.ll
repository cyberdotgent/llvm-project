; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=asm -o - %s | FileCheck %s

target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"
target triple = "os400mi-ibm-os400"

@gb = global i8 -2, align 1
@gs = global i16 65534, align 2

define i32 @main() {
entry:
  %lb = alloca i8
  %ls = alloca i16
  store i8 255, ptr %lb
  store i16 65535, ptr %ls
  %b0 = load i8, ptr @gb
  %b1 = load i8, ptr %lb
  %s0 = load i16, ptr @gs
  %s1 = load i16, ptr %ls
  %sb0 = sext i8 %b0 to i32
  %sb1 = sext i8 %b1 to i32
  %ss0 = sext i16 %s0 to i32
  %ss1 = sext i16 %s1 to i32
  %a = add i32 %sb0, %sb1
  %b = add i32 %ss0, %ss1
  %r = add i32 %a, %b
  ret i32 %r
}

; CHECK: DCL     DD          G000001    CHAR(1)    DEF(C_MEM) POS(5) INIT(X'FE');
; CHECK: DCL     DD          G000002    BIN(2)     UNSGND DEF(C_MEM) POS(7) INIT(65534);
; CHECK: DCL     DD          S000001    CHAR(1)    DEF(C_STACK) POS(5);
; CHECK: DCL     DD          S000002    BIN(2)     UNSGND DEF(C_STACK) POS(9);
; CHECK: DCL     DD          U1_BOX    CHAR(4);
; CHECK:         CPYBLA      S000001,X'FF';
; CHECK:         CPYNV       S000002,65535;
; CHECK:         CPYBLA      U1_BYTE,G000001;
; CHECK:         CPYBLA      U1_BYTE,S000001;
; CHECK:         CPYNV       T000003,G000002;
; CHECK:         CPYNV       T000004,S000002;
; CHECK:         CMPNV(B)    T000001,127/NHI(
; CHECK:         SUBN        T000005,T000001,256;
; CHECK:         CMPNV(B)    T000003,32767/NHI(
; CHECK:         SUBN        T000007,T000003,65536;
