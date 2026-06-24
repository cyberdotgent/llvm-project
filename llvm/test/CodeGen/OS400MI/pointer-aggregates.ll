; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=asm < %s | FileCheck %s

target triple = "os400mi-ibm-os400"
target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"

@target = global i32 7
@holder = global { ptr, i32 } { ptr @target, i32 9 }

define i32 @main() {
entry:
  %pslot = alloca ptr, align 4
  %agg = load { ptr, i32 }, ptr @holder, align 4
  %p = extractvalue { ptr, i32 } %agg, 0
  %same = icmp eq ptr %p, @target
  %chosen = select i1 %same, ptr %p, ptr null
  store ptr %chosen, ptr %pslot, align 4
  %q = load ptr, ptr %pslot, align 4
  %v = load i32, ptr %q, align 4
  %z = zext i1 %same to i32
  %r = add i32 %v, %z
  ret i32 %r
}

; CHECK: DCL     DD          G000001    BIN(4)     DEF(C_MEM) POS(5) INIT(7);
; CHECK: DCL DD G000002 CHAR(8) DEF(C_MEM) POS(9);
; CHECK-NEXT: DCL DD H000001 CHAR(8) DEF(G000002) POS(1) INIT(X'1000000400000009');
; CHECK: CPYNV       {{T[0-9]+}},{{LS_I4|G[0-9]+}};
; CHECK: CPYNV       {{T[0-9]+}},0;
; CHECK: CMPNV(B)    {{T[0-9]+}},268435460/EQ({{B[0-9]+}});
; CHECK: CPYNV       {{T[0-9]+}},{{T[0-9]+}};
; CHECK: CPYNV       {{S[0-9]+}},{{T[0-9]+}};
; CHECK: CPYNV       {{T[0-9]+}},0;
