// RUN: %clang_cc1 -triple os400mi -emit-llvm -O0 -o - %s | FileCheck %s

typedef void *spcptr;

void f(const char *s) {
  spcptr ufcb = __builtin_os400mi_ufcb();
  __builtin_os400mi_char_from_cstr_blank_padded(
      __builtin_os400mi_ufcb_file(ufcb), 10, "QSYSPRT");
  __builtin_os400mi_char_from_cstr_blank_padded(
      __builtin_os400mi_ufcb_library(ufcb), 10, "*LIBL");
  __builtin_os400mi_char_from_cstr_blank_padded(
      __builtin_os400mi_ufcb_member(ufcb), 10, "*FIRST");
  spcptr open = __builtin_os400mi_sysptr_program("QSYS", "QDMCOPEN");
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

  spcptr ext = __builtin_os400mi_sysptr_program("LLVMWORK", "EXTUPPER");
  spcptr in = __builtin_os400mi_native_char(16);
  spcptr outp = __builtin_os400mi_native_char(16);
  __builtin_os400mi_char_from_cstr_blank_padded(in, 16, "abc");
  spcptr ol = __builtin_os400mi_ol2(in, outp);
  __builtin_os400mi_callx(ext, ol);
  __builtin_os400mi_char_to_cstr((char *)s, 16, outp);
  spcptr bin = __builtin_os400mi_native_bin4();
  __builtin_os400mi_native_bin4_set(bin, 7);
  (void)__builtin_os400mi_native_bin4_get(bin);
}

// CHECK: call ptr @llvm.os400mi.ufcb()
// CHECK: call ptr @llvm.os400mi.ufcb.file(ptr {{.*}})
// CHECK: call void @llvm.os400mi.char.from.cstr.blank.padded(ptr {{.*}}, i32 10, ptr {{.*}})
// CHECK: call ptr @llvm.os400mi.ufcb.library(ptr {{.*}})
// CHECK: call void @llvm.os400mi.char.from.cstr.blank.padded(ptr {{.*}}, i32 10, ptr {{.*}})
// CHECK: call ptr @llvm.os400mi.ufcb.member(ptr {{.*}})
// CHECK: call void @llvm.os400mi.char.from.cstr.blank.padded(ptr {{.*}}, i32 10, ptr {{.*}})
// CHECK: call ptr @llvm.os400mi.sysptr.program(ptr {{.*}}, ptr {{.*}})
// CHECK: call void @llvm.os400mi.callx.1(ptr {{.*}}, ptr {{.*}})
// CHECK: call ptr @llvm.os400mi.ufcb.outbuf(ptr {{.*}})
// CHECK: call void @llvm.os400mi.char.fill(ptr {{.*}}, i32 132, i32 32)
// CHECK: call void @llvm.os400mi.char.from.cstr(ptr {{.*}}, i32 132, ptr {{.*}})
// CHECK: call ptr @llvm.os400mi.ufcb.odp(ptr {{.*}})
// CHECK: call i16 @llvm.os400mi.odp.dcb.put(ptr {{.*}})
// CHECK: call ptr @llvm.os400mi.dm.put.wait.option()
// CHECK: call ptr @llvm.os400mi.spcptr.null()
// CHECK: call void @llvm.os400mi.callx.3(ptr {{.*}}, ptr {{.*}}, ptr {{.*}}, ptr {{.*}})
// CHECK: call ptr @llvm.os400mi.sysptr.program(ptr {{.*}}, ptr {{.*}})
// CHECK: call ptr @llvm.os400mi.native.char(i32 16)
// CHECK: call ptr @llvm.os400mi.native.char(i32 16)
// CHECK: call ptr @llvm.os400mi.ol.2(ptr {{.*}}, ptr {{.*}})
// CHECK: call void @llvm.os400mi.callx(ptr {{.*}}, ptr {{.*}})
// CHECK: call void @llvm.os400mi.char.to.cstr(ptr {{.*}}, i32 16, ptr {{.*}})
// CHECK: call ptr @llvm.os400mi.native.bin4()
// CHECK: call void @llvm.os400mi.native.bin4.set(ptr {{.*}}, i32 7)
// CHECK: call i32 @llvm.os400mi.native.bin4.get(ptr {{.*}})
