; RUN: rm -f %t.mi %t.mi.jsonl
; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=asm -o %t.mi %s
; RUN: FileCheck --check-prefix=MAP --input-file=%t.mi.jsonl %s

target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"
target triple = "os400mi-ibm-os400"

define i32 @main() !dbg !5 {
entry:
  %x = add i32 40, 2, !dbg !9
  ret i32 %x, !dbg !10
}

; MAP: {"mi_name":"MAIN","kind":"function","name_class":"function","max_name_length":48,"collision":false,"original":"main","source_location":{"file":"map.c","line":1}}
; MAP-NEXT: {"mi_name":"B000001","kind":"basic_block","name_class":"label","name_ordinal":1,"max_name_length":48,"collision":false,"hash":"{{[0-9A-F]+}}","original":"entry","source_location":{"file":"map.c","line":2,"column":3}}
; MAP-NEXT: {"mi_name":"T000001","kind":"temp","name_class":"temp","name_ordinal":1,"max_name_length":48,"collision":false,"hash":"{{[0-9A-F]+}}","original":"x","source_location":{"file":"map.c","line":2,"column":3},"size":4,"alignment":4}

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, producer: "os400mi-test", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug)
!1 = !DIFile(filename: "map.c", directory: "/tmp")
!2 = !{i32 7, !"Dwarf Version", i32 4}
!3 = !{i32 2, !"Debug Info Version", i32 3}
!4 = !{}
!5 = distinct !DISubprogram(name: "main", scope: !1, file: !1, line: 1, type: !6, scopeLine: 1, spFlags: DISPFlagDefinition, unit: !0, retainedNodes: !4)
!6 = !DISubroutineType(types: !7)
!7 = !{}
!9 = !DILocation(line: 2, column: 3, scope: !5)
!10 = !DILocation(line: 3, column: 3, scope: !5)
