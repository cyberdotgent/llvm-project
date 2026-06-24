// RUN: %clang -target os400mi-ibm-os400 -c %s -o %t.o -### 2>&1 | FileCheck %s --check-prefix=COMPILE
// RUN: %clang -target os400mi-ibm-os400 -ffreestanding %s -o %t.mi -### 2>&1 | FileCheck %s --check-prefix=LINK
// RUN: rm -rf %t.dir
// RUN: mkdir -p %t.dir
// RUN: touch %t.dir/libio.a
// RUN: %clang -target os400mi-ibm-os400 -ffreestanding %s -L%t.dir -lio -o %t.mi -### 2>&1 | FileCheck %s --check-prefix=LIB
// RUN: %clang -target os400mi-ibm-os400 -ffreestanding %s -L%t.dir -l:libio.a -o %t.mi -### 2>&1 | FileCheck %s --check-prefix=LIB

// COMPILE: "-cc1"
// COMPILE-SAME: "-triple" "os400mi-ibm-os400"
// COMPILE-SAME: "-emit-llvm-bc"
// COMPILE-SAME: "-flto=full"
// COMPILE-SAME: "-o" "{{.*}}.o"
// COMPILE-NOT: "llc"

// LINK: "-cc1"
// LINK-SAME: "-emit-llvm-bc"
// LINK: "{{.*}}llvm-link"
// LINK-SAME: "-o" "{{[^"]*}}os400mi-link{{[^"]*}}.bc"
// LINK: "{{.*}}opt"
// LINK-SAME: "-o" "{{[^"]*}}os400mi-opt{{[^"]*}}.bc"
// LINK-SAME: "-passes=internalize,globaldce"
// LINK-SAME: "-internalize-public-api-list=_start,main"
// LINK: "{{.*}}llc"
// LINK-SAME: "-mtriple=os400mi-ibm-os400"
// LINK-SAME: "-filetype=asm"
// LINK-SAME: "-o" "{{.*}}.mi"
// LINK-SAME: "{{[^"]*}}os400mi-opt{{[^"]*}}.bc"

// LIB: "{{.*}}llvm-link"
// LIB-SAME: "{{.*}}libio.a"
// LIB: "{{.*}}opt"
// LIB: "{{.*}}llc"

int main(void) { return 0; }
