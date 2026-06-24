// RUN: %clang_cc1 -triple os400mi -emit-llvm -O0 -o - %s | FileCheck %s

typedef void *spcptr;

void f(const char *s) {
  spcptr ufcb = __builtin_os400mi_ufcb_qsysprt();
  spcptr open = __builtin_os400mi_sysptr_sept(12);
  __builtin_os400mi_callx1(open, ufcb);
  spcptr out = __builtin_os400mi_ufcb_outbuf(ufcb);
  __builtin_os400mi_char_fill(out, 132, ' ');
  __builtin_os400mi_char_from_cstr(out, 132, s);
  spcptr odp = __builtin_os400mi_ufcb_odp(ufcb);
  unsigned short put_entry = __builtin_os400mi_odp_dcb_put(odp);
  spcptr put = __builtin_os400mi_sysptr_sept(put_entry);
  spcptr option = __builtin_os400mi_dm_put_wait_option();
  spcptr null_ptr = __builtin_os400mi_spcptr_null();
  __builtin_os400mi_callx3(put, ufcb, option, null_ptr);
}

// CHECK: call ptr @llvm.os400mi.ufcb.qsysprt()
// CHECK: call ptr @llvm.os400mi.sysptr.sept(i16 12)
// CHECK: call void @llvm.os400mi.callx.1(ptr {{.*}}, ptr {{.*}})
// CHECK: call ptr @llvm.os400mi.ufcb.outbuf(ptr {{.*}})
// CHECK: call void @llvm.os400mi.char.fill(ptr {{.*}}, i32 132, i32 32)
// CHECK: call void @llvm.os400mi.char.from.cstr(ptr {{.*}}, i32 132, ptr {{.*}})
// CHECK: call ptr @llvm.os400mi.ufcb.odp(ptr {{.*}})
// CHECK: call i16 @llvm.os400mi.odp.dcb.put(ptr {{.*}})
// CHECK: call ptr @llvm.os400mi.dm.put.wait.option()
// CHECK: call ptr @llvm.os400mi.spcptr.null()
// CHECK: call void @llvm.os400mi.callx.3(ptr {{.*}}, ptr {{.*}}, ptr {{.*}}, ptr {{.*}})
