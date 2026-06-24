// RUN: %clang_cc1 -triple os400mi-ibm-os400 -emit-llvm -o - %s | FileCheck %s

int runtime_hooks(int status) {
  __builtin_os400mi_runtime_startup();
  return __builtin_os400mi_runtime_terminate(status);
}

// CHECK-LABEL: define{{.*}} i32 @runtime_hooks
// CHECK: call void @llvm.os400mi.runtime.startup()
// CHECK: call i32 @llvm.os400mi.runtime.terminate(i32
// CHECK: declare void @llvm.os400mi.runtime.startup()
// CHECK: declare i32 @llvm.os400mi.runtime.terminate(i32)
