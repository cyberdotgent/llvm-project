; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=asm -o - %s | FileCheck %s

target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"
target triple = "os400mi-ibm-os400"

define i32 @main() {
  %p = alloca i32
  store i32 42, ptr %p
  %v = load i32, ptr %p
  ret i32 %v
}

; CHECK: DCL     DD          C_MEM      CHAR(256) BDRY(16);
; CHECK: DCL     SPCPTR      .C_BASE    INIT(C_MEM);
; CHECK: DCL     DD          S000001    BIN(4)     DEF(C_MEM) POS(5);
; CHECK: DCL     DD          T000001    BIN(4);
; CHECK: ENTRY MAIN INT;
; CHECK:         CPYNV       S000001,42;
; CHECK:         CPYNV       T000001,S000001;
; CHECK:         CPYNV       MAIN_RC,T000001;
; CHECK:         B           .MAIN;
