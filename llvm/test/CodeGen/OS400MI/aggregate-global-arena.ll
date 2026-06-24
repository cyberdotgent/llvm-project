; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=asm -o - %s | FileCheck %s
; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=asm -o %t.mi %s
; RUN: FileCheck %s --check-prefix=MAP --input-file=%t.mi.jsonl

target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"
target triple = "os400mi-ibm-os400"

@bytes = global [4 x i8] [i8 1, i8 2, i8 -1, i8 0], align 1
@pair = global { i8, i16, i32 } { i8 1, i16 4660, i32 -1 }, align 4
@zeros = global [2 x i16] zeroinitializer, align 2

define i32 @main() {
entry:
  %bp = getelementptr [4 x i8], ptr @bytes, i32 0, i32 2
  %bv = load i8, ptr %bp
  %pp = getelementptr { i8, i16, i32 }, ptr @pair, i32 0, i32 1
  %pv = load i16, ptr %pp
  %zp0 = getelementptr [2 x i16], ptr @zeros, i32 0, i32 0
  %z0 = load i16, ptr %zp0
  %zv = zext i8 %bv to i32
  %zp = zext i16 %pv to i32
  %zz = zext i16 %z0 to i32
  %sum0 = add i32 %zv, %zp
  %sum = add i32 %sum0, %zz
  ret i32 %sum
}

; CHECK: DCL DD G000001 CHAR(4) DEF(C_MEM) POS(5);
; CHECK-NEXT: DCL DD H000001 CHAR(4) DEF(G000001) POS(1) INIT(X'0102FF00');
; CHECK-NEXT: DCL DD G000002 CHAR(8) DEF(C_MEM) POS(9);
; CHECK-NEXT: DCL DD H000002 CHAR(8) DEF(G000002) POS(1) INIT(X'01001234FFFFFFFF');
; CHECK-NEXT: DCL DD G000003 CHAR(4) DEF(C_MEM) POS(17);
; CHECK-NEXT: DCL DD H000003 CHAR(4) DEF(G000003) POS(1) INIT(X'00000000');
; CHECK: ADDSPP      .LS,.C_BASE,OFF;
; CHECK: CPYNV       MAIN_RC,{{T[0-9]+}};

; MAP: {"mi_name":"G000001","kind":"aggregate","name_class":"global","name_ordinal":1,"max_name_length":48,"collision":false,"hash":"{{[0-9A-F]+}}","original":"bytes","arena_offset":4,"size":4,"alignment":1,"encoding":"raw-bytes"}
; MAP: {"mi_name":"H000001","kind":"initializer_chunk","name_class":"helper","name_ordinal":1,"max_name_length":48,"collision":false,"hash":"{{[0-9A-F]+}}","original":"G000001","arena_offset":4,"size":4,"alignment":1,"encoding":"raw-bytes"}
; MAP: {"mi_name":"G000002","kind":"aggregate","name_class":"global","name_ordinal":2,"max_name_length":48,"collision":false,"hash":"{{[0-9A-F]+}}","original":"pair","arena_offset":8,"size":8,"alignment":4,"encoding":"raw-bytes"}
; MAP: {"mi_name":"H000002","kind":"initializer_chunk","name_class":"helper","name_ordinal":2,"max_name_length":48,"collision":false,"hash":"{{[0-9A-F]+}}","original":"G000002","arena_offset":8,"size":8,"alignment":1,"encoding":"raw-bytes"}
; MAP: {"mi_name":"G000003","kind":"aggregate","name_class":"global","name_ordinal":3,"max_name_length":48,"collision":false,"hash":"{{[0-9A-F]+}}","original":"zeros","arena_offset":16,"size":4,"alignment":2,"encoding":"raw-bytes"}
; MAP: {"mi_name":"H000003","kind":"initializer_chunk","name_class":"helper","name_ordinal":3,"max_name_length":48,"collision":false,"hash":"{{[0-9A-F]+}}","original":"G000003","arena_offset":16,"size":4,"alignment":1,"encoding":"raw-bytes"}
