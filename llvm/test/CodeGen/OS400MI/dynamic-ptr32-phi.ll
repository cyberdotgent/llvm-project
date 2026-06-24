; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=asm -o - %s | FileCheck %s

target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"
target triple = "os400mi-ibm-os400"

define i32 @main() {
entry:
  %arr = alloca [4 x i32]
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %next, %body ]
  %p = getelementptr [4 x i32], ptr %arr, i32 0, i32 %i
  store i32 %i, ptr %p
  %next = add i32 %i, 1
  %c = icmp slt i32 %next, 3
  br i1 %c, label %body, label %done

body:
  br label %loop

done:
  %q = getelementptr [4 x i32], ptr %arr, i32 0, i32 2
  %v = load i32, ptr %q
  ret i32 %v
}

; CHECK: DCL     DD          C_STACK    CHAR(256) BDRY(16);
; CHECK: DCL     SPCPTR      .S_BASE    INIT(C_STACK);
; CHECK: DCL     DD          S000001    CHAR(16)    DEF(C_STACK) POS(5);
; CHECK: DCL     SPCPTR      .LS;
; CHECK: DCL     DD          OFF       BIN(4);
; CHECK: DCL     SPC         LOADSTORE BAS(.LS);
; CHECK: DCL     DD          LS_I4     BIN(4)     DIR        POS(1);
; CHECK: ENTRY MAIN INT;
; CHECK: B000001:
; CHECK-NEXT:         B           B000005;
; CHECK: B000002:
; CHECK-NEXT:         ADDN        T000002,T000001,T000001;
; CHECK-NEXT:         ADDN        T000003,T000002,T000002;
; CHECK-NEXT:         ADDN        T000004,T000003,536870916;
; CHECK-NEXT:         CPYNV       OFF,T000004;
; CHECK:              ADDSPP      .LS,.S_BASE,OFF;
; CHECK:              CPYNV       LS_I4,T000001;
; CHECK:         CMPNV(B)    T000005,3/LO(B000003);
; CHECK-NEXT:         B           B000004;
; CHECK: B000004:
; CHECK-NEXT:         CPYNV       OFF,536870924;
; CHECK:              ADDSPP      .LS,.S_BASE,OFF;
; CHECK:              CPYNV       T000006,LS_I4;
; CHECK: B000005:
; CHECK-NEXT:         CPYNV       T000001,0;
; CHECK-NEXT:         B           B000002;
; CHECK: B000009:
; CHECK-NEXT:         CPYNV       T000001,T000005;
; CHECK-NEXT:         B           B000002;
