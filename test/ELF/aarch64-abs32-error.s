// RUN: llvm-mc -filetype=obj -triple=aarch64-none-freebsd %s -o %t
// RUN: not ld.lld -shared %t -o %t2 2>&1 | FileCheck %s
// REQUIRES: aarch64

.word sym+99999999999

// CHECK: R_AARCH64_ABS32 out of range
