//===-- OS400MITargetInfo.h - OS/400 MI Target Impl -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_OS400MI_TARGETINFO_OS400MITARGETINFO_H
#define LLVM_LIB_TARGET_OS400MI_TARGETINFO_OS400MITARGETINFO_H

namespace llvm {

class Target;

Target &getTheOS400MITarget();

} // namespace llvm

#endif // LLVM_LIB_TARGET_OS400MI_TARGETINFO_OS400MITARGETINFO_H
