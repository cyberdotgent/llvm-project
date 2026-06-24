// RUN: rm -rf %t.sysroot
// RUN: mkdir -p %t.sysroot/usr/local/os400mi-ibm-os400/include
// RUN: mkdir -p %t.sysroot/usr/local/os400mi-ibm-os400/lib
// RUN: touch %t.sysroot/usr/local/os400mi-ibm-os400/lib/crt0.o
// RUN: touch %t.sysroot/usr/local/os400mi-ibm-os400/lib/libc.a
// RUN: touch %t.sysroot/usr/local/os400mi-ibm-os400/lib/libos400mi.a
// RUN: %clang --target=os400mi-ibm-os400 --sysroot=%t.sysroot -### -c %s 2>&1 | FileCheck %s
// RUN: %clang --target=os400mi-ibm-os400 --sysroot=%t.sysroot -### %s -o %t.mi 2>&1 | FileCheck %s --check-prefix=LINK
// RUN: %clang --target=os400mi-ibm-os400 --sysroot=%t.sysroot -ffreestanding -### %s -o %t.mi 2>&1 | FileCheck %s --check-prefix=FREESTANDING

// CHECK: "-internal-isystem" "[[SYSROOT:.*]]/usr/local/os400mi-ibm-os400/include"

// LINK: "{{.*}}llvm-link"
// LINK-SAME: "{{.*}}crt0.o"
// LINK-SAME: "{{.*}}libc.a"
// LINK-SAME: "{{.*}}libos400mi.a"
// LINK: "{{.*}}llc"

// FREESTANDING: "{{.*}}llvm-link"
// FREESTANDING-NOT: "crt0.o"
// FREESTANDING-NOT: "libc.a"
// FREESTANDING-NOT: "libos400mi.a"
// FREESTANDING: "{{.*}}llc"
