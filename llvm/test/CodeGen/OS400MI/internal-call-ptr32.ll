; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=obj -o - %s | FileCheck %s
; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=obj -o %t.mi %s
; RUN: FileCheck --check-prefix=MAP %s < %t.mi.jsonl

target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"
target triple = "os400mi-ibm-os400"

define internal ptr @idp(ptr %p) {
entry:
  ret ptr %p
}

define internal i32 @loadp(ptr %p) {
entry:
  %v = load i32, ptr %p, align 4
  ret i32 %v
}

define i32 @main() {
entry:
  %x = alloca i32, align 4
  store i32 42, ptr %x, align 4
  %q = call ptr @idp(ptr %x)
  %r = call i32 @loadp(ptr %q)
  ret i32 %r
}

; CHECK: DCL     DD          F000001R    BIN(4)     UNSGND;
; CHECK: DCL     DD          F000001A1    BIN(4)     UNSGND;
; CHECK: DCL     DD          F000002A1    BIN(4)     UNSGND;
; CHECK: ENTRY MAIN INT;
; CHECK: CPYNV       F000001A1,4;
; CHECK: CALLI       F000001, *, .F000001;
; CHECK: CPYNV       [[PTR:T[0-9]+]],F000001R;
; CHECK: CPYNV       F000002A1,[[PTR]];
; CHECK: CALLI       F000002, *, .F000002;
; CHECK: ENTRY F000001 INT;
; CHECK: CPYNV       F000001R,F000001A1;
; CHECK: ENTRY F000002 INT;
; CHECK: CPYNV       OFF,F000002A1;
; CHECK: ADDSPP      .LS,.C_BASE,OFF;
; CHECK: CPYNV       [[LOAD:T[0-9]+]],LS_I4;
; CHECK: CPYNV       F000002R,[[LOAD]];
; CHECK: PEND;

; MAP: "mi_name":"MAIN_FRAME"
; MAP-SAME: "kind":"frame"
; MAP-SAME: "original":"main"
; MAP-SAME: "arena_offset":4
; MAP-SAME: "size":4
; MAP-SAME: "encoding":"static-arena"
; MAP: "mi_name":"F000001_FRAME"
; MAP-SAME: "kind":"frame"
; MAP-SAME: "original":"idp"
; MAP: "mi_name":"F000002_FRAME"
; MAP-SAME: "kind":"frame"
; MAP-SAME: "original":"loadp"
