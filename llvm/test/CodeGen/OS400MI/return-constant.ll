; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=obj -o - %s | FileCheck %s
; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=asm -o - %s | FileCheck %s

target datalayout = "E-p:32:32-i8:32:32-i16:32:32-i32:32:32-i64:32:32-n32-S32"
target triple = "os400mi-ibm-os400"

define i32 @main() {
  ret i32 7
}

; CHECK: DCL     SPCPTR      ARGC@      PARM;
; CHECK: DCL     SPCPTR      ARGV@      PARM;
; CHECK: DCL     DD          MAIN_RC    BIN(4);
; CHECK: DCL     INSPTR      .MAIN;
; CHECK: ENTRY * (PARM_LIST) EXT;
; CHECK:         CALLI       MAIN, *, .MAIN;
; CHECK:         RTX         *;
; CHECK: ENTRY MAIN INT;
; CHECK:         CPYNV       MAIN_RC,7;
; CHECK:         B           .MAIN;
; CHECK:         PEND;
