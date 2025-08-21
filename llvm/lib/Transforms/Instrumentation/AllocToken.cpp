//===- AllocToken.cpp - Allocation token instrumentation ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements AllocToken, an instrumentation pass that
// replaces allocation calls with token-enabled versions.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Instrumentation/AllocToken.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Analysis.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/xxhash.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

using namespace llvm;

#define DEBUG_TYPE "alloc-token"

namespace {

//===--- Constants --------------------------------------------------------===//

enum class TokenMode : unsigned {
  /// Incrementally increasing token ID.
  Increment = 0,

  /// Simple mode that returns a statically-assigned random token ID.
  Random = 1,

  /// Token ID based on allocated type hash.
  TypeHash = 2,

  /// Token ID based on allocated type hash, where the top half ID-space is
  /// reserved for types that contain pointers and the bottom half for types
  /// that do not contain pointers.
  TypeHashPointerSplit = 3,

  // Mode count - keep last
  ModeCount
};

//===--- Command-line options ---------------------------------------------===//

struct ModeParser : public cl::parser<unsigned> {
  ModeParser(cl::Option &O) : cl::parser<unsigned>(O) {}
  bool parse(cl::Option &O, StringRef ArgName, StringRef Arg, unsigned &Value) {
    if (cl::parser<unsigned>::parse(O, ArgName, Arg, Value))
      return true;
    if (Value >= static_cast<unsigned>(TokenMode::ModeCount))
      return O.error("'" + Arg + "' value invalid");
    return false;
  }
};

cl::opt<unsigned, false, ModeParser>
    ClMode("alloc-token-mode", cl::desc("Token assignment mode"), cl::Hidden,
           cl::init(static_cast<unsigned>(TokenMode::TypeHashPointerSplit)));

cl::opt<std::string> ClFuncPrefix("alloc-token-prefix",
                                  cl::desc("The allocation function prefix"),
                                  cl::Hidden, cl::init("__alloc_token_"));

cl::opt<uint64_t> ClMaxTokens("alloc-token-max",
                              cl::desc("Maximum number of tokens (0 = no max)"),
                              cl::Hidden, cl::init(0));

cl::opt<bool>
    ClFastABI("alloc-token-fast-abi",
              cl::desc("The token ID is encoded in the function name"),
              cl::Hidden, cl::init(false));

// Instrument libcalls only by default - compatible allocators only need to take
// care of providing standard allocation functions. With extended coverage, also
// instrument non-libcall allocation function calls with !alloc_token_hint
// metadata.
cl::opt<bool>
    ClExtended("alloc-token-extended",
               cl::desc("Extend coverage to custom allocation functions"),
               cl::Hidden, cl::init(false));

// C++ defines ::operator new (and variants) as replaceable (vs. standard
// library versions), which are nobuiltin, and are therefore not covered by
// isAllocationFn(). Cover by default, as users of AllocToken are already
// required to provide token-aware allocation functions (no defaults).
cl::opt<bool> ClCoverReplaceableNew("alloc-token-cover-replaceable-new",
                                    cl::desc("Cover replaceable operator new"),
                                    cl::Hidden, cl::init(true));

// strdup-family functions only operate on strings, covering them does not make
// sense in most cases.
cl::opt<bool>
    ClCoverStrdup("alloc-token-cover-strdup",
                  cl::desc("Cover strdup-family allocation functions"),
                  cl::Hidden, cl::init(false));

cl::opt<uint64_t> ClFallbackToken(
    "alloc-token-fallback",
    cl::desc("The default fallback token where none could be determined"),
    cl::Hidden, cl::init(0));

//===--- Statistics -------------------------------------------------------===//

STATISTIC(NumFunctionsInstrumented, "Functions instrumented");
STATISTIC(NumAllocations, "Allocations found");

//===----------------------------------------------------------------------===//

/// Returns the !alloc_token_hint metadata if available.
///
/// Expected format is: !{<type-name>, <contains-pointer>}
MDNode *getAllocTokenHintMetadata(const CallBase &CB) {
  MDNode *Ret = nullptr;
  if (auto *II = dyn_cast<IntrinsicInst>(&CB)) {
    auto *MDV = cast<MetadataAsValue>(II->getArgOperand(0));
    Ret = cast<MDNode>(MDV->getMetadata());
    // If the intrinsic has an empty MDNode, type inference failed.
    if (Ret->getNumOperands() == 0)
      return nullptr;
  } else {
    Ret = CB.getMetadata("alloc_token_hint");
    if (!Ret)
      return nullptr;
  }
  assert(Ret->getNumOperands() == 2 && "bad !alloc_token_hint");
  assert(isa<MDString>(Ret->getOperand(0)));
  assert(isa<ConstantAsMetadata>(Ret->getOperand(1)));
  return Ret;
}

bool containsPointer(MDNode *MD) {
  ConstantAsMetadata *C = cast<ConstantAsMetadata>(MD->getOperand(1));
  auto *CI = cast<ConstantInt>(C->getValue());
  return CI->getValue().getBoolValue();
}

/// Infers the allocated IR type by analyzing instruction users on best effort
/// basis. Note: Likely to fail if an allocation is not immediately followed by
/// use of the allocation.
///
/// NOTE: Language frontends should prefer emitting !alloc_token_hint.
Type *inferTypeFromUsers(const CallBase &CB) {
  SmallVector<const Value *, 4> PtrAliases{&CB};
  SmallPtrSet<const Value *, 4> Visited{&CB};

  unsigned Idx = 0;
  while (Idx < PtrAliases.size()) {
    const Value *V = PtrAliases[Idx++];

    for (const User *U : V->users()) {
      if (auto *GEP = dyn_cast<GetElementPtrInst>(U))
        return GEP->getSourceElementType();
      if (auto *SI = dyn_cast<StoreInst>(U)) {
        if (SI->getPointerOperand() == V)
          return SI->getValueOperand()->getType();
      }
      if (auto *LI = dyn_cast<LoadInst>(U)) {
        if (LI->getPointerOperand() == V)
          return LI->getType();
      }

      // Recursively find users of aliases:
      if (auto *SI = dyn_cast<StoreInst>(U); SI && SI->getValueOperand() == V) {
        // The pointer is being stored somewhere else; find all loads from
        // that location.
        const Value *Alias = SI->getPointerOperand();
        for (const User *AU : Alias->users()) {
          if (isa<LoadInst>(AU) && Visited.insert(AU).second)
            PtrAliases.push_back(AU);
        }
      }
      // %casted_ptr = bitcast ptr %our_pointer to ptr
      if (isa<CastInst>(U) && U->getType()->isPointerTy() &&
          Visited.insert(U).second)
        PtrAliases.push_back(U);
    }
  }

  return nullptr;
}

/// Determines if an LLVM Type is or contains a pointer.
///
/// NOTE: Language frontends should prefer emitting !alloc_token_hint.
bool containsPointer(Type *Ty) {
  SmallPtrSet<const StructType *, 4> Visited;
  auto TypeContainsPtr = [&Visited](auto &&self, Type *Ty) -> bool {
    if (Ty->isPointerTy())
      return true; // base case
    // Handle arrays.
    if (Ty->isArrayTy())
      return self(self, Ty->getArrayElementType());
    // Traverse structs.
    if (auto *STy = dyn_cast<StructType>(Ty)) {
      if (!Visited.insert(STy).second)
        return false; // already visited
      for (Type *ElementType : STy->elements()) {
        if (self(self, ElementType))
          return true;
      }
    }
    return false;
  };
  return TypeContainsPtr(TypeContainsPtr, Ty);
}

class ModeBase {
public:
  explicit ModeBase(uint64_t MaxTokens) : MaxTokens(MaxTokens) {}

protected:
  uint64_t boundedToken(uint64_t Val) const {
    return MaxTokens ? Val % MaxTokens : Val;
  }

  const uint64_t MaxTokens;
};

/// Implementation for TokenMode::Increment.
class IncrementMode : public ModeBase {
public:
  using ModeBase::ModeBase;

  uint64_t operator()(const CallBase &CB, OptimizationRemarkEmitter &) {
    return boundedToken(Counter++);
  }

private:
  uint64_t Counter = 0;
};

/// Implementation for TokenMode::Random.
class RandomMode : public ModeBase {
public:
  RandomMode(uint64_t MaxTokens, std::unique_ptr<RandomNumberGenerator> RNG)
      : ModeBase(MaxTokens), RNG(std::move(RNG)) {}
  uint64_t operator()(const CallBase &CB, OptimizationRemarkEmitter &) {
    return boundedToken((*RNG)());
  }

private:
  std::unique_ptr<RandomNumberGenerator> RNG;
};

/// Implementation for TokenMode::TypeHash. The implementation ensures
/// hashes are stable across different compiler invocations. Uses xxHash as the
/// hash function.
class TypeHashMode : public ModeBase {
public:
  using ModeBase::ModeBase;

  uint64_t operator()(const CallBase &CB, OptimizationRemarkEmitter &ORE) {
    auto [N, H] = getHash(CB, ORE);
    const bool Valid =
        std::visit([](auto &&TyOrMD) { return TyOrMD != nullptr; }, N);
    return Valid ? boundedToken(H) : H;
  }

protected:
  std::pair<std::variant<MDNode *, Type *>, uint64_t>
  getHash(const CallBase &CB, OptimizationRemarkEmitter &ORE) {
    if (MDNode *N = getAllocTokenHintMetadata(CB)) {
      MDString *S = cast<MDString>(N->getOperand(0));
      return {N, xxHash64(S->getString())};
    }
    // Fallback.
    remarkNoHint(CB, ORE);
    if (Type *T = inferTypeFromUsers(CB)) {
      // Printing the full IR type is expensive, cache the hash.
      auto It = HashCache.find(T);
      uint64_t H;
      if (It == HashCache.end()) {
        std::string TypeName;
        raw_string_ostream RSO(TypeName);
        T->print(RSO);
        H = HashCache[T] = xxHash64(RSO.str());
      } else {
        H = It->second;
      }
      return {T, H};
    }

    return {static_cast<MDNode *>(nullptr), ClFallbackToken};
  }

  /// Remark that there was no precise type information.
  void remarkNoHint(const CallBase &CB, OptimizationRemarkEmitter &ORE) {
    ORE.emit([&] {
      ore::NV FuncNV("Function", CB.getParent()->getParent());
      const Function *Callee = CB.getCalledFunction();
      ore::NV CalleeNV("Callee", Callee ? Callee->getName() : "<unknown>");
      return OptimizationRemark(DEBUG_TYPE, "NoAllocTokenHint", &CB)
             << "Call to '" << CalleeNV << "' in '" << FuncNV
             << "' without source-level type token";
    });
  }

private:
  DenseMap<Type *, uint64_t> HashCache;
};

/// Implementation for TokenMode::TypeHashPointerSplit.
class TypeHashPointerSplitMode : public TypeHashMode {
public:
  using TypeHashMode::TypeHashMode;

  uint64_t operator()(const CallBase &CB, OptimizationRemarkEmitter &ORE) {
    if (MaxTokens == 1)
      return 0;
    const uint64_t HalfTokens =
        (MaxTokens ? MaxTokens : std::numeric_limits<uint64_t>::max()) / 2;
    const auto SourceAndHash = getHash(CB, ORE);
    bool Valid = false;
    const bool ContainsPtr = std::visit(
        [&](auto &&TyOrMD) {
          if (!TyOrMD)
            return false;
          Valid = true;
          return containsPointer(TyOrMD);
        },
        SourceAndHash.first);
    if (!Valid)
      return SourceAndHash.second;
    uint64_t Hash = SourceAndHash.second % HalfTokens; // base hash
    if (ContainsPtr)
      Hash += HalfTokens;
    return Hash;
  }
};

// Apply opt overrides.
AllocTokenOptions &&transformOptionsFromCl(AllocTokenOptions &&Opts) {
  if (!Opts.MaxTokens.has_value())
    Opts.MaxTokens = ClMaxTokens;
  Opts.FastABI |= ClFastABI;
  Opts.Extended |= ClExtended;
  return std::move(Opts);
}

class AllocToken {
public:
  explicit AllocToken(AllocTokenOptions Opts, Module &M,
                      ModuleAnalysisManager &MAM)
      : Options(transformOptionsFromCl(std::move(Opts))), Mod(M),
        FAM(MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager()),
        Mode(IncrementMode(*Options.MaxTokens)) {
    switch (static_cast<TokenMode>(ClMode.getValue())) {
    case TokenMode::Increment:
      break;
    case TokenMode::Random:
      Mode.emplace<RandomMode>(*Options.MaxTokens, M.createRNG(DEBUG_TYPE));
      break;
    case TokenMode::TypeHash:
      Mode.emplace<TypeHashMode>(*Options.MaxTokens);
      break;
    case TokenMode::TypeHashPointerSplit:
      Mode.emplace<TypeHashPointerSplitMode>(*Options.MaxTokens);
      break;
    case TokenMode::ModeCount:
      llvm_unreachable("");
      break;
    }
  }

  bool instrumentFunction(Function &F);

private:
  /// Returns true for !isAllocationFn() functions that are also eligible for
  /// instrumentation.
  bool isInstrumentableLibFunc(LibFunc Func) const;

  /// Returns true for isAllocationFn() functions that we should ignore.
  bool ignoreInstrumentableLibFunc(LibFunc Func) const;

  /// Replace a call/invoke with a call/invoke to the allocation function
  /// with token ID.
  void replaceAllocationCall(CallBase *CB, LibFunc Func,
                             OptimizationRemarkEmitter &ORE,
                             const TargetLibraryInfo &TLI);

  /// Return replacement function for a LibFunc that takes a token ID.
  FunctionCallee getTokenAllocFunction(const CallBase &CB, uint64_t TokenID,
                                       LibFunc OriginalFunc);

  /// Lower alloc_token_* intrinsics.
  void replaceIntrinsicInst(IntrinsicInst *II, OptimizationRemarkEmitter &ORE);

  /// Return the token ID from metadata in the call.
  uint64_t getToken(const CallBase &CB, OptimizationRemarkEmitter &ORE) {
    return std::visit([&](auto &&Mode) { return Mode(CB, ORE); }, Mode);
  }

  const AllocTokenOptions Options;
  Module &Mod;
  FunctionAnalysisManager &FAM;
  // Cache for replacement functions.
  DenseMap<std::pair<LibFunc, uint64_t>, FunctionCallee> TokenAllocFunctions;
  // Selected mode.
  std::variant<IncrementMode, RandomMode, TypeHashMode,
               TypeHashPointerSplitMode>
      Mode;
};

bool AllocToken::instrumentFunction(Function &F) {
  // Do not apply any instrumentation for naked functions.
  if (F.hasFnAttribute(Attribute::Naked))
    return false;
  // Don't touch available_externally functions, their actual body is elsewhere.
  if (F.getLinkage() == GlobalValue::AvailableExternallyLinkage)
    return false;

  auto &ORE = FAM.getResult<OptimizationRemarkEmitterAnalysis>(F);
  auto &TLI = FAM.getResult<TargetLibraryAnalysis>(F);
  SmallVector<std::pair<CallBase *, LibFunc>, 4> AllocCalls;
  SmallVector<IntrinsicInst *, 4> IntrinsicInsts;

  // Collect all allocation calls to avoid iterator invalidation.
  for (Instruction &I : instructions(F)) {
    // Collect all alloc_token_* intrinsics.
    if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(&I);
        II && II->getIntrinsicID() == Intrinsic::alloc_token_id) {
      IntrinsicInsts.emplace_back(II);
      continue;
    }

    // Only instrument functions that have the sanitize_alloc_token attribute.
    if (!F.hasFnAttribute(Attribute::SanitizeAllocToken))
      continue;
    if (F.hasFnAttribute(Attribute::DisableSanitizerInstrumentation))
      continue;

    auto *CB = dyn_cast<CallBase>(&I);
    if (!CB)
      continue;
    const Function *Callee = CB->getCalledFunction();
    if (!Callee)
      continue;

    // Ignore nobuiltin of the CallBase, so that we can cover nobuiltin libcalls
    // if requested via isInstrumentableLibFunc(). Note that isAllocationFn() is
    // returning false for nobuiltin calls.
    LibFunc Func;
    if (TLI.getLibFunc(*Callee, Func)) {
      if (ignoreInstrumentableLibFunc(Func))
        continue;
      if (isInstrumentableLibFunc(Func) || isAllocationFn(CB, &TLI))
        AllocCalls.emplace_back(CB, Func);
    } else if (Options.Extended && getAllocTokenHintMetadata(*CB)) {
      AllocCalls.emplace_back(CB, NotLibFunc);
    }
  }

  bool Modified = false;

  if (!AllocCalls.empty()) {
    for (auto &[CB, Func] : AllocCalls) {
      replaceAllocationCall(CB, Func, ORE, TLI);
    }
    NumAllocations += AllocCalls.size();
    NumFunctionsInstrumented++;
    Modified = true;
  }

  for (auto *II : IntrinsicInsts) {
    replaceIntrinsicInst(II, ORE);
    Modified = true;
  }

  return Modified;
}

bool AllocToken::isInstrumentableLibFunc(LibFunc Func) const {
  switch (Func) {
  case LibFunc_posix_memalign:
  case LibFunc_size_returning_new:
  case LibFunc_size_returning_new_hot_cold:
  case LibFunc_size_returning_new_aligned:
  case LibFunc_size_returning_new_aligned_hot_cold:
    return true;
  case LibFunc_Znwj:
  case LibFunc_ZnwjRKSt9nothrow_t:
  case LibFunc_ZnwjSt11align_val_t:
  case LibFunc_ZnwjSt11align_val_tRKSt9nothrow_t:
  case LibFunc_Znwm:
  case LibFunc_Znwm12__hot_cold_t:
  case LibFunc_ZnwmRKSt9nothrow_t:
  case LibFunc_ZnwmRKSt9nothrow_t12__hot_cold_t:
  case LibFunc_ZnwmSt11align_val_t:
  case LibFunc_ZnwmSt11align_val_t12__hot_cold_t:
  case LibFunc_ZnwmSt11align_val_tRKSt9nothrow_t:
  case LibFunc_ZnwmSt11align_val_tRKSt9nothrow_t12__hot_cold_t:
  case LibFunc_Znaj:
  case LibFunc_ZnajRKSt9nothrow_t:
  case LibFunc_ZnajSt11align_val_t:
  case LibFunc_ZnajSt11align_val_tRKSt9nothrow_t:
  case LibFunc_Znam:
  case LibFunc_Znam12__hot_cold_t:
  case LibFunc_ZnamRKSt9nothrow_t:
  case LibFunc_ZnamRKSt9nothrow_t12__hot_cold_t:
  case LibFunc_ZnamSt11align_val_t:
  case LibFunc_ZnamSt11align_val_t12__hot_cold_t:
  case LibFunc_ZnamSt11align_val_tRKSt9nothrow_t:
  case LibFunc_ZnamSt11align_val_tRKSt9nothrow_t12__hot_cold_t:
    return ClCoverReplaceableNew;
  default:
    return false;
  }
}

bool AllocToken::ignoreInstrumentableLibFunc(LibFunc Func) const {
  switch (Func) {
  case LibFunc_strdup:
  case LibFunc_dunder_strdup:
  case LibFunc_strndup:
  case LibFunc_dunder_strndup:
    return !ClCoverStrdup;
  default:
    return false;
  }
}

void AllocToken::replaceAllocationCall(CallBase *CB, LibFunc Func,
                                       OptimizationRemarkEmitter &ORE,
                                       const TargetLibraryInfo &TLI) {
  uint64_t TokenID = getToken(*CB, ORE);

  FunctionCallee TokenAlloc = getTokenAllocFunction(*CB, TokenID, Func);
  if (!TokenAlloc)
    return;

  IRBuilder<> IRB(CB);

  // Original args.
  SmallVector<Value *, 4> NewArgs{CB->args()};
  if (!Options.FastABI) {
    // Add token ID.
    NewArgs.push_back(
        ConstantInt::get(Type::getInt64Ty(Mod.getContext()), TokenID));
  }
  assert(TokenAlloc.getFunctionType()->getNumParams() == NewArgs.size());

  // Preserve invoke vs call semantics for exception handling.
  CallBase *NewCall;
  if (auto *II = dyn_cast<InvokeInst>(CB)) {
    NewCall = IRB.CreateInvoke(TokenAlloc, II->getNormalDest(),
                               II->getUnwindDest(), NewArgs);
  } else {
    NewCall = IRB.CreateCall(TokenAlloc, NewArgs);
    cast<CallInst>(NewCall)->setTailCall(CB->isTailCall());
  }
  NewCall->setCallingConv(CB->getCallingConv());
  NewCall->copyMetadata(*CB);
  NewCall->setAttributes(CB->getAttributes());

  // Replace all uses and delete the old call.
  CB->replaceAllUsesWith(NewCall);
  CB->eraseFromParent();
}

FunctionCallee AllocToken::getTokenAllocFunction(const CallBase &CB,
                                                 uint64_t TokenID,
                                                 LibFunc OriginalFunc) {
  std::optional<std::pair<LibFunc, uint64_t>> Key;
  if (OriginalFunc != NotLibFunc) {
    Key = std::make_pair(OriginalFunc, Options.FastABI ? TokenID : 0);
    auto It = TokenAllocFunctions.find(*Key);
    if (LLVM_LIKELY(It != TokenAllocFunctions.end()))
      return It->second;
  }

  const Function *Callee = CB.getCalledFunction();
  if (!Callee)
    return FunctionCallee();
  const FunctionType *OldFTy = Callee->getFunctionType();
  if (OldFTy->isVarArg())
    return FunctionCallee();
  // Copy params, and append token ID type.
  LLVMContext &C = Mod.getContext();
  Type *RetTy = OldFTy->getReturnType();
  SmallVector<Type *, 4> NewParams{OldFTy->params()};
  std::string TokenAllocName = ClFuncPrefix;
  if (Options.FastABI) {
    TokenAllocName += utostr(TokenID) + "_";
  } else {
    NewParams.push_back(Type::getInt64Ty(C)); // token ID
  }
  FunctionType *NewFTy = FunctionType::get(RetTy, NewParams, false);
  // Remove leading '_' - we add our own.
  StringRef No_ = Callee->getName().drop_while([](char C) { return C == '_'; });
  TokenAllocName += No_;
  FunctionCallee TokenAlloc = Mod.getOrInsertFunction(TokenAllocName, NewFTy);
  if (Function *F = dyn_cast<Function>(TokenAlloc.getCallee()))
    F->copyAttributesFrom(Callee); // preserve attrs

  if (Key.has_value())
    TokenAllocFunctions[*Key] = TokenAlloc;
  return TokenAlloc;
}

void AllocToken::replaceIntrinsicInst(IntrinsicInst *II,
                                      OptimizationRemarkEmitter &ORE) {
  assert(II->getIntrinsicID() == Intrinsic::alloc_token_id);

  uint64_t TokenID = getToken(*II, ORE);
  Value *V = ConstantInt::get(Type::getInt64Ty(Mod.getContext()), TokenID);
  II->replaceAllUsesWith(V);
  II->eraseFromParent();
}

} // namespace

AllocTokenPass::AllocTokenPass(AllocTokenOptions Opts)
    : Options(std::move(Opts)) {}

PreservedAnalyses AllocTokenPass::run(Module &M, ModuleAnalysisManager &MAM) {
  AllocToken Pass(Options, M, MAM);
  bool Modified = false;

  for (Function &F : M) {
    if (LLVM_LIKELY(F.empty()))
      continue; // declaration
    Modified |= Pass.instrumentFunction(F);
  }

  return Modified ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
