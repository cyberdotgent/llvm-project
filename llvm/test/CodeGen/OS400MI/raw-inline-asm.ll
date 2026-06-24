; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=obj -o - %s | FileCheck %s

target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"
target triple = "os400mi-ibm-os400"

module asm "DCL SPCPTR @SEPT BASPCO;"
module asm "DCL SPCPTR .SEPT(6440) BAS(@SEPT);"

define i32 @main() {
entry:
  call void asm sideeffect "        CPYBLA      MAIN_RC,MAIN_RC;", ""()
  ret i32 0
}

; CHECK: DCL SPCPTR @SEPT BASPCO;
; CHECK-NEXT: DCL SPCPTR .SEPT(6440) BAS(@SEPT);
; CHECK: ENTRY MAIN INT;
; CHECK: {{^}}        CPYBLA      MAIN_RC,MAIN_RC;
; CHECK: {{^}}        CPYNV       MAIN_RC,0;
