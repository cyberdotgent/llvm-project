; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=obj -o - %s | FileCheck %s
; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=obj -o %t.mi %s
; RUN: FileCheck --check-prefix=MAP %s < %t.mi.jsonl

target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"
target triple = "os400mi-ibm-os400"

define internal i8 @id8(i8 %x) {
entry:
  ret i8 %x
}

define internal i16 @id16(i16 %x) {
entry:
  ret i16 %x
}

define internal i1 @id1(i1 %x) {
entry:
  ret i1 %x
}

define i32 @main() {
entry:
  %a = call i8 @id8(i8 -1)
  %b = zext i8 %a to i32
  %c = call i16 @id16(i16 -1)
  %d = zext i16 %c to i32
  %e = add i32 %b, %d
  %f = call i1 @id1(i1 true)
  %g = zext i1 %f to i32
  %h = add i32 %e, %g
  ret i32 %h
}

; CHECK: DCL     DD          F000001R    BIN(4);
; CHECK: DCL     DD          F000001RB   CHAR(4)    DEF(F000001R) POS(1);
; CHECK: DCL     DD          F000001A1    BIN(4);
; CHECK: DCL     DD          F000001A1B   CHAR(4)    DEF(F000001A1) POS(1);
; CHECK: DCL     DD          F000002R    BIN(4);
; CHECK: DCL     DD          F000002RB   CHAR(4)    DEF(F000002R) POS(1);
; CHECK: DCL     DD          F000002A1    BIN(4);
; CHECK: DCL     DD          F000002A1B   CHAR(4)    DEF(F000002A1) POS(1);
; CHECK: DCL     DD          F000003R    BIN(4);
; CHECK: DCL     DD          F000003RB   CHAR(4)    DEF(F000003R) POS(1);
; CHECK: DCL     DD          F000003A1    BIN(4);
; CHECK: DCL     DD          F000003A1B   CHAR(4)    DEF(F000003A1) POS(1);
; CHECK: ENTRY MAIN INT;
; CHECK: CPYNV       F000001A1,-1;
; CHECK: CPYNV       [[M8:T[0-9]+]],255;
; CHECK: AND         F000001A1B,F000001A1B,[[M8]]B;
; CHECK: CALLI       F000001, *, .F000001;
; CHECK: CPYNV       [[R8:T[0-9]+]],F000001R;
; CHECK: CPYNV       [[RM8:T[0-9]+]],255;
; CHECK: AND         [[R8]]B,[[R8]]B,[[RM8]]B;
; CHECK: CPYNV       F000002A1,-1;
; CHECK: CPYNV       [[M16:T[0-9]+]],65535;
; CHECK: AND         F000002A1B,F000002A1B,[[M16]]B;
; CHECK: CALLI       F000002, *, .F000002;
; CHECK: CPYNV       [[R16:T[0-9]+]],F000002R;
; CHECK: CPYNV       [[RM16:T[0-9]+]],65535;
; CHECK: AND         [[R16]]B,[[R16]]B,[[RM16]]B;
; CHECK: CPYNV       F000003A1,1;
; CHECK: CPYNV       [[M1:T[0-9]+]],1;
; CHECK: AND         F000003A1B,F000003A1B,[[M1]]B;
; CHECK: CALLI       F000003, *, .F000003;
; CHECK: CPYNV       [[R1:T[0-9]+]],F000003R;
; CHECK: CPYNV       [[RM1:T[0-9]+]],1;
; CHECK: AND         [[R1]]B,[[R1]]B,[[RM1]]B;
; CHECK: ENTRY F000001 INT;
; CHECK: CPYNV       [[C8:T[0-9]+]],255;
; CHECK: AND         F000001A1B,F000001A1B,[[C8]]B;
; CHECK: CPYNV       F000001R,F000001A1;
; CHECK: CPYNV       [[RET8:T[0-9]+]],255;
; CHECK: AND         F000001RB,F000001RB,[[RET8]]B;
; CHECK: ENTRY F000002 INT;
; CHECK: CPYNV       [[C16:T[0-9]+]],65535;
; CHECK: AND         F000002A1B,F000002A1B,[[C16]]B;
; CHECK: CPYNV       F000002R,F000002A1;
; CHECK: CPYNV       [[RET16:T[0-9]+]],65535;
; CHECK: AND         F000002RB,F000002RB,[[RET16]]B;
; CHECK: ENTRY F000003 INT;
; CHECK: CPYNV       [[C1:T[0-9]+]],1;
; CHECK: AND         F000003A1B,F000003A1B,[[C1]]B;
; CHECK: CPYNV       F000003R,F000003A1;
; CHECK: CPYNV       [[RET1:T[0-9]+]],1;
; CHECK: AND         F000003RB,F000003RB,[[RET1]]B;
; CHECK: PEND;

; MAP: "mi_name":"F000001"
; MAP-SAME: "kind":"function"
; MAP-SAME: "original":"id8"
; MAP: "mi_name":"F000002"
; MAP-SAME: "kind":"function"
; MAP-SAME: "original":"id16"
; MAP: "mi_name":"F000003"
; MAP-SAME: "kind":"function"
; MAP-SAME: "original":"id1"
; MAP: "kind":"narrow_mask"
