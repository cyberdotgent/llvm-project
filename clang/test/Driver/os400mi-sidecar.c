// RUN: %clang -target os400mi-ibm-os400 -c %s -o %t.mi -### 2>&1 | FileCheck %s

// CHECK: "-cc1"
// CHECK-SAME: "-triple" "os400mi-ibm-os400"
// CHECK-SAME: "-object-file-name={{.*}}.mi"

int main(void) { return 0; }
