; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=asm -o %t.mi %s
; RUN: FileCheck %s --input-file=%t.mi
; RUN: %python -c "import sys; bad=[(i+1,len(l.rstrip('\n'))) for i,l in enumerate(open(sys.argv[1])) if len(l.rstrip('\n')) > 80]; assert not bad, bad" %t.mi

target datalayout = "E-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:32-f32:32:32-f64:64:64-n32-S32"
target triple = "os400mi-ibm-os400"

@.str = private unnamed_addr constant [38 x i8] c"Hello, world from OS/400 MI/OPM LLVM!\00", align 1

define i32 @main() {
entry:
  %p = ptrtoint ptr @.str to i32
  %low = and i32 %p, 3
  ret i32 %low
}

; CHECK: DCL     DD          L000001    CHAR(38)    DEF(C_MEM) POS(5);
; CHECK: DCL DD H000001 CHAR({{[0-9]+}}) DEF(L000001) POS(1) INIT(X'
; CHECK: DCL DD H000002 CHAR({{[0-9]+}}) DEF(L000001) POS({{[0-9]+}}) INIT(X'
; CHECK: DCL DD H000003 CHAR({{[0-9]+}}) DEF(L000001) POS({{[0-9]+}}) INIT(X'
; CHECK: DCL DD H000004 CHAR({{[0-9]+}}) DEF(L000001) POS({{[0-9]+}}) INIT(X'
