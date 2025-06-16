; Test for special libfuncs not automatically considered allocation functions.
;
; RUN: opt < %s -passes=inferattrs,alloc-partition -S | FileCheck %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

declare {ptr, i64} @__size_returning_new(i64)

; CHECK-LABEL: @test_extra_libfuncs
define ptr @test_extra_libfuncs() sanitize_alloc_partition {
entry:
  ; CHECK: call {{.*}} @__alloc_partition_size_returning_new(
  %srn = call {ptr, i64} @__size_returning_new(i64 10), !alloc_partition_hint !0
  %ptr1  = extractvalue {ptr, i64} %srn, 0
  ret ptr %ptr1
}

declare ptr @_Znwm(i64) nobuiltin allocsize(0)
declare ptr @_Znam(i64) nobuiltin allocsize(0)

; CHECK-LABEL: @test_replaceable_new
define ptr @test_replaceable_new() sanitize_alloc_partition {
entry:
  ; CHECK: call ptr @__alloc_partition_Znwm(
  %ptr1 = call ptr @_Znwm(i64 32), !alloc_partition_hint !0
  ; CHECK: call ptr @__alloc_partition_Znam(
  %ptr2 = call ptr @_Znam(i64 64), !alloc_partition_hint !0
  ret ptr %ptr1
}

!0 = !{!"int"}
