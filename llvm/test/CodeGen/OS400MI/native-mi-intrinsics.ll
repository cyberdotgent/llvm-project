; RUN: llc -mtriple=os400mi < %s | FileCheck %s

@hello = internal constant [14 x i8] c"Hello, world!\00"

define i32 @main() {
entry:
  %ufcb = call ptr @llvm.os400mi.ufcb.qsysprt()
  %open = call ptr @llvm.os400mi.sysptr.sept(i16 12)
  call void @llvm.os400mi.callx.1(ptr %open, ptr %ufcb)
  %out = call ptr @llvm.os400mi.ufcb.outbuf(ptr %ufcb)
  call void @llvm.os400mi.char.fill(ptr %out, i32 132, i32 32)
  call void @llvm.os400mi.char.from.cstr(ptr %out, i32 132, ptr @hello)
  %odp = call ptr @llvm.os400mi.ufcb.odp(ptr %ufcb)
  %putent = call i16 @llvm.os400mi.odp.dcb.put(ptr %odp)
  %put = call ptr @llvm.os400mi.sysptr.sept(i16 %putent)
  %opt = call ptr @llvm.os400mi.dm.put.wait.option()
  %null = call ptr @llvm.os400mi.spcptr.null()
  call void @llvm.os400mi.callx.3(ptr %put, ptr %ufcb, ptr %opt, ptr %null)
  %close = call ptr @llvm.os400mi.sysptr.sept(i16 11)
  call void @llvm.os400mi.callx.1(ptr %close, ptr %ufcb)
  ret i32 0
}

declare ptr @llvm.os400mi.ufcb.qsysprt()
declare ptr @llvm.os400mi.sysptr.sept(i16)
declare void @llvm.os400mi.callx.1(ptr, ptr)
declare ptr @llvm.os400mi.ufcb.outbuf(ptr)
declare void @llvm.os400mi.char.fill(ptr, i32, i32)
declare void @llvm.os400mi.char.from.cstr(ptr, i32, ptr)
declare ptr @llvm.os400mi.ufcb.odp(ptr)
declare i16 @llvm.os400mi.odp.dcb.put(ptr)
declare ptr @llvm.os400mi.dm.put.wait.option()
declare ptr @llvm.os400mi.spcptr.null()
declare void @llvm.os400mi.callx.3(ptr, ptr, ptr, ptr)

; CHECK-DAG: DCL     SPCPTR      @SEPT     BASPCO;
; CHECK-DAG: DCL     SPCPTR      .OFCB     INIT(OFCB);
; CHECK-DAG: DCL     DD          OUTBUF    CHAR(132) BAS(.OFCB-OUTBUF);
; CHECK-DAG: DCL     DD          NCHAR     CHAR(1)    BAS(.NCHAR);
; CHECK: CALLX       .SEPT(12),{{[^,]+}},*;
; CHECK: CPYBREP     OUTBUF," ";
; CHECK: CMPBLA(B)   LS_I1,X'00'/EQ(
; CHECK: CPYBLA      NCHAR,LS_I1;
; CHECK: CALLX       .SEPT(PUT-ENTRY),{{[^,]+}},*;
; CHECK: CALLX       .SEPT(11),{{[^,]+}},*;
