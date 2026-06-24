; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=obj -o - %s | FileCheck %s
; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=obj -o %t.mi %s
; RUN: FileCheck --check-prefix=MAP %s < %t.mi.jsonl

target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"
target triple = "os400mi-ibm-os400"

define i32 @main() {
entry:
  ret i32 7
}

define i32 @_start() {
entry:
  %rc = call i32 @main()
  ret i32 %rc
}

define i32 @unused_from_archive(i32 %x, ...) {
entry:
  ret i32 %x
}

; CHECK: ENTRY * (PARM_LIST) EXT;
; CHECK: CALLI       MAIN, *, .MAIN;
; CHECK: ENTRY MAIN INT;
; CHECK: CALLI       F000001, *, .F000001;
; CHECK: CPYNV       MAIN_RC,{{T[0-9]+}};
; CHECK: B           .MAIN;
; CHECK: ENTRY F000001 INT;

; MAP: "mi_name":"MAIN"
; MAP-SAME: "original":"_start"
; MAP: "mi_name":"F000001"
; MAP-SAME: "original":"main"
; MAP-NOT: unused_from_archive
