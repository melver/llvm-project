// RUN: %clang_cc1 -E -fsanitize=alloc-token %s -o - | FileCheck --check-prefix=CHECK-ALLOC-TOKEN %s
// RUN: %clang_cc1 -E  %s -o - | FileCheck --check-prefix=CHECK-NO-ALLOC-TOKEN %s

#if __has_feature(alloc_token)
int AllocTokenEnabled();
#else
int AllocTokenDisabled();
#endif

// CHECK-ALLOC-TOKEN: AllocTokenEnabled

// CHECK-NO-ALLOC-TOKEN: AllocTokenDisabled
