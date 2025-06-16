//===- AllocPartition.h - Allocation-partition hints instrumentation ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the AllocPartitionPass, an instrumentation pass that
// replaces allocation calls with partition-hinted versions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_ALLOCPARTITION_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_ALLOCPARTITION_H

#include "llvm/IR/Analysis.h"
#include "llvm/IR/PassManager.h"
#include <optional>

namespace llvm {

class Module;

struct AllocPartitionOptions {
  std::optional<uint64_t> MaxPartitions;
  bool FastABI = false;
  bool Extended = false;
  AllocPartitionOptions() = default;
};

/// A module pass that instruments heap allocations to use partitioned
/// allocation functions based on allocated type or calling function.
class AllocPartitionPass : public PassInfoMixin<AllocPartitionPass> {
public:
  LLVM_ABI explicit AllocPartitionPass(AllocPartitionOptions Opts = {});
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
  static bool isRequired() { return true; }

private:
  const AllocPartitionOptions Options;
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_ALLOCPARTITION_H
