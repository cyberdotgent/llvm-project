// RUN: %clang_cc1 -triple powerpc64-unknown-linux-gnu -target-feature +ppcas \
// RUN:   -emit-obj %s -o %t
// RUN: llvm-objdump -d --mattr=+ppcas %t | FileCheck %s

__asm__("settag\n"
        "txer 1, 0, 35\n"
        "lq 24, 0(3), 6\n"
        "selri. 25, 25, 0, 41\n"
        "mcrxrt 0\n");

// CHECK: settag
// CHECK: txer 1, 0, 35
// CHECK: lq 24, 0(3), 6
// CHECK: selri. 25, 25, 0, 41
// CHECK: mcrxrt 0
