; RUN: not llc -mtriple=os400mi-ibm-os400 -filetype=asm -o - %s 2>&1 | FileCheck %s

target triple = "os400mi-ibm-os400"

module asm "DCL DD OVERLONG-LINE CHAR(1) DEF(C_MEM) POS(1) INIT(X'0000000000000000000000000000000000000000');"

define i32 @main() {
entry:
  ret i32 0
}

; CHECK: MI source lines no longer than 80 characters
