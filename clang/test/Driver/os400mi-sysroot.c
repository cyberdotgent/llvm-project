// RUN: %clang --target=os400mi-ibm-os400 --sysroot=%S/Inputs/os400mi-sysroot -### -c %s 2>&1 | FileCheck %s

// CHECK: "-internal-isystem" "[[SYSROOT:.*/Inputs/os400mi-sysroot]]/usr/local/os400mi-ibm-os400/include"
