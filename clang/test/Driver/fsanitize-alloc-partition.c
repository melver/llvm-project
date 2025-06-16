// RUN: %clang --target=x86_64-linux-gnu -fsanitize=alloc-partition %s -### 2>&1 | FileCheck %s --check-prefix=CHECK-PARTITIONED-ALLOC
// CHECK-PARTITIONED-ALLOC: "-fsanitize=alloc-partition"

// RUN: %clang --target=x86_64-linux-gnu -fsanitize=alloc-partition -fno-sanitize=alloc-partition %s -### 2>&1 | FileCheck %s --check-prefix=CHECK-NO-PARTITIONED-ALLOC
// CHECK-NO-PARTITIONED-ALLOC-NOT: "-fsanitize=alloc-partition"

// RUN: %clang --target=x86_64-linux-gnu -flto -fvisibility=hidden -fno-sanitize-ignorelist -fsanitize=alloc-partition,undefined,cfi %s -### 2>&1 | FileCheck %s --check-prefix=CHECK-COMPATIBLE
// CHECK-COMPATIBLE: "-fsanitize={{.*}}alloc-partition"

// RUN: %clang --target=x86_64-linux-gnu -fsanitize=alloc-partition -fsanitize-minimal-runtime %s -### 2>&1 | FileCheck %s --check-prefix=CHECK-MINIMAL
// CHECK-MINIMAL: "-fsanitize=alloc-partition"
// CHECK-MINIMAL: "-fsanitize-minimal-runtime"

// RUN: %clang --target=arm-arm-non-eabi -fsanitize=alloc-partition %s -### 2>&1 | FileCheck %s -check-prefix=CHECK-BAREMETAL
// RUN: %clang --target=aarch64-none-elf -fsanitize=alloc-partition %s -### 2>&1 | FileCheck %s -check-prefix=CHECK-BAREMETAL
// CHECK-BAREMETAL: "-fsanitize=alloc-partition"

// RUN: not %clang --target=x86_64-linux-gnu -fsanitize=alloc-partition,address %s -### 2>&1 | FileCheck %s --check-prefix=CHECK-INCOMPATIBLE-ADDRESS
// CHECK-INCOMPATIBLE-ADDRESS: error: invalid argument '-fsanitize=alloc-partition' not allowed with '-fsanitize=address'

// RUN: not %clang --target=x86_64-linux-gnu -fsanitize=alloc-partition,memory %s -### 2>&1 | FileCheck %s --check-prefix=CHECK-INCOMPATIBLE-MEMORY
// CHECK-INCOMPATIBLE-MEMORY: error: invalid argument '-fsanitize=alloc-partition' not allowed with '-fsanitize=memory'

// RUN: not %clang --target=x86_64-linux-gnu -fsanitize=alloc-partition -fsanitize-trap=alloc-partition %s -### 2>&1 | FileCheck %s --check-prefix=CHECK-INCOMPATIBLE-TRAP
// CHECK-INCOMPATIBLE-TRAP: error: unsupported argument 'alloc-partition' to option '-fsanitize-trap='

// RUN: not %clang --target=x86_64-linux-gnu %s -fsanitize=alloc-partition -fsanitize-recover=alloc-partition -### 2>&1 | FileCheck %s --check-prefix=CHECK-INCOMPATIBLE-RECOVER
// CHECK-INCOMPATIBLE-RECOVER: unsupported argument 'alloc-partition' to option '-fsanitize-recover='

// RUN: %clang --target=x86_64-linux-gnu -fsanitize=alloc-partition -fsanitize-alloc-partition-max=4294967295 %s -### 2>&1 | FileCheck -check-prefix=CHECK-MAX %s
// CHECK-MAX: "-fsanitize-alloc-partition-max=4294967295"
// RUN: not %clang --target=x86_64-linux-gnu -fsanitize=alloc-partition -fsanitize-alloc-partition-max=0 %s -### 2>&1 | FileCheck -check-prefix=CHECK-INVALID-MAX %s
// RUN: not %clang --target=x86_64-linux-gnu -fsanitize=alloc-partition -fsanitize-alloc-partition-max=-1 %s -### 2>&1 | FileCheck -check-prefix=CHECK-INVALID-MAX %s
// CHECK-INVALID-MAX: error: invalid value

// RUN: %clang --target=x86_64-linux-gnu -fsanitize=alloc-partition -fsanitize-alloc-partition-fast-abi %s -### 2>&1 | FileCheck -check-prefix=CHECK-FASTABI %s
// CHECK-FASTABI: "-fsanitize-alloc-partition-fast-abi"
// RUN: %clang --target=x86_64-linux-gnu -fsanitize=alloc-partition -fsanitize-alloc-partition-fast-abi -fno-sanitize-alloc-partition-fast-abi %s -### 2>&1 | FileCheck -check-prefix=CHECK-NOFASTABI %s
// CHECK-NOFASTABI-NOT: "-fsanitize-alloc-partition-fast-abi"

// RUN: %clang --target=x86_64-linux-gnu -fsanitize=alloc-partition -fsanitize-alloc-partition-extended %s -### 2>&1 | FileCheck -check-prefix=CHECK-EXTENDED %s
// CHECK-EXTENDED: "-fsanitize-alloc-partition-extended"
// RUN: %clang --target=x86_64-linux-gnu -fsanitize=alloc-partition -fsanitize-alloc-partition-extended -fno-sanitize-alloc-partition-extended %s -### 2>&1 | FileCheck -check-prefix=CHECK-NOEXTENDED %s
// CHECK-NOEXTENDED-NOT: "-fsanitize-alloc-partition-extended"
