; RUN: llc -mtriple=os400mi-ibm-os400 -filetype=obj < %s | FileCheck %s

define i32 @main() {
entry:
  %r = call i32 @pick(i32 2)
  ret i32 %r
}

define i32 @pick(i32 %x) {
entry:
  switch i32 %x, label %default [
    i32 1, label %one
    i32 2, label %two
  ]

one:
  ret i32 11

two:
  ret i32 22

default:
  ret i32 33
}

; CHECK: CMPNV(B)    {{T[0-9]+}},1/EQ(
; CHECK: CMPNV(B)    {{T[0-9]+}},2/EQ(
; CHECK: B           B

