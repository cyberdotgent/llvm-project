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
  %zv = zext i8 %bv to i32
  %zp = zext i16 %pv to i32
  %sum = add i32 %zv, %zp
  ret i32 %sum
}

; CHECK: DCL DD G000001 CHAR(4) DEF(C_MEM) POS(5) INIT(X'0102FF00');
; CHECK: DCL DD G000002 CHAR(8) DEF(C_MEM) POS(9) INIT(X'01001234FFFFFFFF');
; CHECK: DCL DD G000003 CHAR(4) DEF(C_MEM) POS(17) INIT(X'00000000');
; CHECK: ADDSPP      .LS,.C_BASE,OFF;
; CHECK: CPYNV       MAIN_RC,{{T[0-9]+}};

; MAP: {"mi_name":"G000001","kind":"aggregate","name_class":"global","name_ordinal":1,"max_name_length":48,"collision":false,"hash":"{{[0-9A-F]+}}","original":"bytes","arena_offset":4,"size":4,"alignment":1,"encoding":"raw-bytes"}
; MAP: {"mi_name":"G000002","kind":"aggregate","name_class":"global","name_ordinal":2,"max_name_length":48,"collision":false,"hash":"{{[0-9A-F]+}}","original":"pair","arena_offset":8,"size":8,"alignment":4,"encoding":"raw-bytes"}
; MAP: {"mi_name":"G000003","kind":"aggregate","name_class":"global","name_ordinal":3,"max_name_length":48,"collision":false,"hash":"{{[0-9A-F]+}}","original":"zeros","arena_offset":16,"size":4,"alignment":2,"encoding":"raw-bytes"}
