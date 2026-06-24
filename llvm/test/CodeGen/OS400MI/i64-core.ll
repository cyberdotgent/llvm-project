; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=asm < %s | FileCheck %s

target triple = "os400mi-ibm-os400"
target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"

@g = global i64 81985529216486895

define i32 @main() {
entry:
  %slot = alloca i64, align 4
  %v = load i64, ptr @g, align 4
  %a = add i64 %v, 5
  %s = sub i64 %a, 2
  %l = shl i64 %s, 3
  store i64 %l, ptr %slot, align 4
  %r = load i64, ptr %slot, align 4
  %c = icmp ult i64 %r, 9223372036854775807
  %z = zext i1 %c to i32
  %t = trunc i64 %r to i32
  %ret = add i32 %z, %t
  ret i32 %ret
}

; CHECK: DCL DD G000001 CHAR(8) DEF(C_MEM) POS(5) INIT(X'0123456789ABCDEF');
; CHECK: DCL     DD          [[SLOT:S[0-9]+]]    CHAR(8)    DEF(C_STACK)
; CHECK: DCL     DD          [[I64:T[0-9]+]]    CHAR(8);
; CHECK: DCL     DD          [[I64]]H    BIN(4)     DEF([[I64]]) POS(1);
; CHECK: DCL     DD          [[I64]]U    BIN(4)     UNSGND DEF([[I64]]) POS(1);
; CHECK: DCL     DD          [[I64]]L    BIN(4)     UNSGND DEF([[I64]]) POS(5);
; CHECK: DCL     DD          [[I64]]LB   CHAR(4)    DEF([[I64]]) POS(5);
; CHECK: CPYBLA      {{T[0-9]+}},X'0000000000000005';
; CHECK: XOR        {{T[0-9]+}},{{T[0-9]+}},{{T[0-9]+}};
; CHECK: AND        {{T[0-9]+}},{{T[0-9]+}},{{T[0-9]+}};
; CHECK: CPYBTLLS    {{T[0-9]+}},{{T[0-9]+}},1;
; CHECK: CPYBLA      {{T[0-9]+}},X'FFFFFFFFFFFFFFFF';
; CHECK: XOR        {{T[0-9]+}},{{T[0-9]+}},{{T[0-9]+}};
; CHECK: AND        {{T[0-9]+}},{{T[0-9]+}},{{T[0-9]+}};
; CHECK: CPYBTLLS    {{T[0-9]+}},{{T[0-9]+}},3;
; CHECK: CMPNV(B)    {{T[0-9]+}}U,{{T[0-9]+}}U/LO({{B[0-9]+}});
; CHECK: CMPNV(B)    {{T[0-9]+}}L,{{T[0-9]+}}L/LO({{B[0-9]+}});
; CHECK: CPYBLA      {{T[0-9]+}}B,{{T[0-9]+}}LB;
