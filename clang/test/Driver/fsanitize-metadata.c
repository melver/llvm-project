// RUN: %clang -target x86_64-linux-gnu %s -### 2>&1 | FileCheck %s
// CHECK-NOT: -fexperimental-sanitize-metadata

// RUN: %clang -target x86_64-linux-gnu -fexperimental-sanitize-metadata=bad_arg %s -### 2>&1 | \
// RUN:   FileCheck -check-prefix=CHECK-INVALID %s
// CHECK-INVALID: error: unsupported argument 'bad_arg' to option '-fexperimental-sanitize-metadata='

// RUN: %clang -target x86_64-linux-gnu -fexperimental-sanitize-metadata=covered %s -### 2>&1 | \
// RUN:   FileCheck -check-prefix=CHECK-COVERED %s
// CHECK-COVERED: "-fexperimental-sanitize-metadata-covered"

// RUN: %clang -target x86_64-linux-gnu -fexperimental-sanitize-metadata=atomics %s -### 2>&1 | \
// RUN:   FileCheck -check-prefix=CHECK-ATOMICS %s
// CHECK-ATOMICS: "-fexperimental-sanitize-metadata-atomics"

// RUN: %clang -target x86_64-linux-gnu \
// RUN:   -fexperimental-sanitize-metadata=covered,atomics %s -### 2>&1 | \
// RUN:   FileCheck -check-prefix=CHECK-ALL %s
// RUN: %clang -target x86_64-linux-gnu -fexperimental-sanitize-metadata=all %s -### 2>&1 | \
// RUN:   FileCheck -check-prefix=CHECK-ALL %s
// CHECK-ALL: "-fexperimental-sanitize-metadata-covered"
// CHECK-ALL: "-fexperimental-sanitize-metadata-atomics"
