; RUN: rm -f %t.mi %t.mi.jsonl
; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=asm -o %t.mi %s
; RUN: FileCheck --check-prefix=MI --input-file=%t.mi %s
; RUN: FileCheck --check-prefix=MAP --input-file=%t.mi.jsonl %s

target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"
target triple = "os400mi-ibm-os400"

define i32 @main() {
entry:
  %p = alloca i32
  store i32 40, ptr %p
  %v = load i32, ptr %p
  %x = add i32 %v, 2
  %c = icmp eq i32 %x, 42
  br i1 %c, label %then, label %else

then:
  ret i32 %x

else:
  ret i32 0
}

; MI: B000001:
; MI: B000002:
; MI: B000003:

; MAP: {"mi_name":"C_MEM","kind":"arena","name_class":"reserved","max_name_length":48,"collision":false,"arena_offset":0,"size":256,"alignment":16}
; MAP-NEXT: {"mi_name":"MAIN_RC","kind":"return_slot","name_class":"reserved","max_name_length":48,"collision":false,"size":4,"alignment":4}
; MAP-NEXT: {"mi_name":"MAIN","kind":"function","name_class":"function","max_name_length":48,"collision":false,"original":"main"}
; MAP-NEXT: {"mi_name":"B000001","kind":"basic_block","name_class":"label","name_ordinal":1,"max_name_length":48,"collision":false,"hash":"{{[0-9A-F]+}}","original":"entry"}
; MAP-NEXT: {"mi_name":"B000002","kind":"basic_block","name_class":"label","name_ordinal":2,"max_name_length":48,"collision":false,"hash":"{{[0-9A-F]+}}","original":"then"}
; MAP-NEXT: {"mi_name":"B000003","kind":"basic_block","name_class":"label","name_ordinal":3,"max_name_length":48,"collision":false,"hash":"{{[0-9A-F]+}}","original":"else"}
; MAP-NEXT: {"mi_name":"S000001","kind":"local","name_class":"local","name_ordinal":1,"max_name_length":48,"collision":false,"hash":"{{[0-9A-F]+}}","original":"p","arena_offset":4,"size":4,"alignment":4}
; MAP-NEXT: {"mi_name":"T000001","kind":"temp","name_class":"temp","name_ordinal":1,"max_name_length":48,"collision":false,"hash":"{{[0-9A-F]+}}","original":"v","size":4,"alignment":4}
; MAP-NEXT: {"mi_name":"T000002","kind":"temp","name_class":"temp","name_ordinal":2,"max_name_length":48,"collision":false,"hash":"{{[0-9A-F]+}}","original":"x","size":4,"alignment":4}
