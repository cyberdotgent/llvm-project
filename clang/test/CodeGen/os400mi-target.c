// RUN: %clang_cc1 -triple os400mi-ibm-os400 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple os400mi-ibm-os400 -E -dM -x c /dev/null | FileCheck --check-prefix=MACROS %s

// CHECK: target datalayout = "E-p:32:32-i8:32:32-i16:32:32-i32:32:32-i64:32:32-n32-S32"
// CHECK: target triple = "os400mi-ibm-os400"

// MACROS-DAG: #define __BYTE_ORDER__ __ORDER_BIG_ENDIAN__
// MACROS-DAG: #define __OS400MI__ 1
// MACROS-DAG: #define __SIZEOF_INT__ 4
// MACROS-DAG: #define __SIZEOF_LONG__ 4
// MACROS-DAG: #define __SIZEOF_LONG_LONG__ 8
// MACROS-DAG: #define __SIZEOF_POINTER__ 4
// MACROS-DAG: #define __os400mi__ 1

int main(void) { return 0; }
