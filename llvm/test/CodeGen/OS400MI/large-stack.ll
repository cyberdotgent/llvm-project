; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=obj < %s | FileCheck %s

define i32 @main() {
entry:
  %buf = alloca [400 x i8], align 1
  %p = getelementptr [400 x i8], ptr %buf, i32 0, i32 399
  store i8 7, ptr %p, align 1
  %v = load i8, ptr %p, align 1
  %z = zext i8 %v to i32
  ret i32 %z
}

; CHECK: DCL     DD          C_STACK    CHAR(32768) BDRY(16);
