; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=asm -o - %s | FileCheck %s

target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"
target triple = "os400mi-ibm-os400"

define i32 @main() {
entry:
  %bytes = alloca [4 x i8]
  %shorts = alloca [4 x i16]
  %bp = getelementptr [4 x i8], ptr %bytes, i32 0, i32 2
  store i8 42, ptr %bp
  %bv = load i8, ptr %bp
  %bz = zext i8 %bv to i32
  %sp = getelementptr [4 x i16], ptr %shorts, i32 0, i32 2
  store i16 1234, ptr %sp
  %sv = load i16, ptr %sp
  %sz = zext i16 %sv to i32
  %sum = add i32 %bz, %sz
  ret i32 %sum
}

; CHECK: DCL     DD          S000001    CHAR(4)    DEF(C_STACK) POS(5);
; CHECK: DCL     DD          S000002    CHAR(8)    DEF(C_STACK) POS(9);
; CHECK: DCL     DD          LS_I1     CHAR(1)    DIR        POS(1);
; CHECK: DCL     DD          LS_I2     BIN(2)     UNSGND DIR POS(1);
; CHECK: DCL     DD          U1_BOX    CHAR(4);
; CHECK: DCL     DD          U1_NUM    BIN(4)     DEF(U1_BOX) POS(1);
; CHECK: DCL     DD          U1_BYTE   CHAR(1)    DEF(U1_BOX) POS(4);
; CHECK:         CPYNV       OFF,536870918;
; CHECK:              ADDSPP      .LS,.S_BASE,OFF;
; CHECK:              CPYBLA      LS_I1,X'2A';
; CHECK:         CPYBLA      U1_BOX,X'00000000';
; CHECK-NEXT:         CPYBLA      U1_BYTE,LS_I1;
; CHECK-NEXT:         CPYNV       T000001,U1_NUM;
; CHECK:         CPYNV       OFF,536870924;
; CHECK:              ADDSPP      .LS,.S_BASE,OFF;
; CHECK:              CPYNV       LS_I2,1234;
; CHECK:         CPYNV       T000002,LS_I2;
