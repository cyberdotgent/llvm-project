// RUN: %clang_cc1 -triple=os400mi-ibm-os400 -E -dM < /dev/null | FileCheck -match-full-lines %s

// CHECK-DAG: #define __OS400MI__ 1
// CHECK-DAG: #define __OS400__ 1
// CHECK-DAG: #define __SCHAR_WIDTH__ 8
// CHECK-DAG: #define __UCHAR_WIDTH__ 8
// CHECK-DAG: #define __SHRT_WIDTH__ 16
// CHECK-DAG: #define __USHRT_WIDTH__ 16
// CHECK-DAG: #define __INT_WIDTH__ 32
// CHECK-DAG: #define __UINT_WIDTH__ 32
// CHECK-DAG: #define __LONG_WIDTH__ 32
// CHECK-DAG: #define __ULONG_WIDTH__ 32
// CHECK-DAG: #define __LLONG_WIDTH__ 64
// CHECK-DAG: #define __LONG_LONG_WIDTH__ 64
// CHECK-DAG: #define __ULLONG_WIDTH__ 64
