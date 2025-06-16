// RUN: %clang_cc1 -E -fsanitize=alloc-partition %s -o - | FileCheck --check-prefix=CHECK-ALLOCPART %s
// RUN: %clang_cc1 -E  %s -o - | FileCheck --check-prefix=CHECK-NO-ALLOCPART %s

#if __has_feature(alloc_partition)
int AllocPartitionEnabled();
#else
int AllocPartitionDisabled();
#endif

// CHECK-ALLOCPART: AllocPartitionEnabled

// CHECK-NO-ALLOCPART: AllocPartitionDisabled
