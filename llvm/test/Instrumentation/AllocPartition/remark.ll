; RUN: opt < %s -passes=inferattrs,alloc-partition -pass-remarks=alloc-partition -S 2>&1 | FileCheck %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

declare ptr @malloc(i64)

; CHECK-NOT: remark: <unknown>:0:0: Call to 'malloc' in 'test_has_metadata' without !alloc_partition_hint
; CHECK: remark: <unknown>:0:0: Call to 'malloc' in 'test_no_metadata' without !alloc_partition_hint

; CHECK-LABEL: @test_has_metadata
define ptr @test_has_metadata() sanitize_alloc_partition {
entry:
  ; CHECK: call ptr @__alloc_partition_malloc(
  %ptr1 = call ptr @malloc(i64 64), !alloc_partition_hint !0
  ret ptr %ptr1
}

; CHECK-LABEL: @test_no_metadata
define ptr @test_no_metadata() sanitize_alloc_partition {
entry:
  ; CHECK: call ptr @__alloc_partition_malloc(
  %ptr1 = call ptr @malloc(i64 32)
  ret ptr %ptr1
}

!0 = !{!"int"}
