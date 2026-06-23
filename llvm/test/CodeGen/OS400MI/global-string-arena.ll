; RUN: rm -f %t.mi %t.mi.jsonl
; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=asm -o %t.mi %s
; RUN: FileCheck --check-prefix=MI --input-file=%t.mi %s
; RUN: FileCheck --check-prefix=MAP --input-file=%t.mi.jsonl %s

target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"
target triple = "os400mi-ibm-os400"

@g = global i32 7, align 4
@.str = private unnamed_addr constant [3 x i8] c"Hi\00", align 1

define i32 @main() {
entry:
  %v = load i32, ptr @g
  ret i32 %v
}

; MI: DCL     DD          C_MEM      CHAR(256) BDRY(16);
; MI: DCL     DD          G000001    BIN(4)     DEF(C_MEM) POS(5) INIT(7);
; MI: DCL     DD          L000001    CHAR(3)    DEF(C_MEM) POS(9) INIT(X'C88900');
; MI: CPYNV       T000001,G000001;

; MAP: {"mi_name":"C_MEM","kind":"arena","name_class":"reserved","max_name_length":48,"collision":false,"arena_offset":0,"size":256,"alignment":16}
; MAP-NEXT: {"mi_name":"MAIN_RC","kind":"return_slot","name_class":"reserved","max_name_length":48,"collision":false,"size":4,"alignment":4}
; MAP-NEXT: {"mi_name":"MAIN","kind":"function","name_class":"function","max_name_length":48,"collision":false,"original":"main"}
; MAP-NEXT: {"mi_name":"G000001","kind":"global","name_class":"global","name_ordinal":1,"max_name_length":48,"collision":false,"hash":"{{[0-9A-F]+}}","original":"g","arena_offset":4,"size":4,"alignment":4}
; MAP-NEXT: {"mi_name":"L000001","kind":"string","name_class":"literal","name_ordinal":1,"max_name_length":48,"collision":false,"hash":"{{[0-9A-F]+}}","original":".str","arena_offset":8,"size":3,"alignment":1,"encoding":"ibm-037"}
; MAP-NEXT: {"mi_name":"B000001","kind":"basic_block","name_class":"label","name_ordinal":1,"max_name_length":48,"collision":false,"hash":"{{[0-9A-F]+}}","original":"entry"}
; MAP-NEXT: {"mi_name":"T000001","kind":"temp","name_class":"temp","name_ordinal":1,"max_name_length":48,"collision":false,"hash":"{{[0-9A-F]+}}","original":"v","size":4,"alignment":4}
