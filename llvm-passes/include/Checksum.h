#ifndef LLVM_PUF_CHECKSUM_H
#define LLVM_PUF_CHECKSUM_H

#include "Utils.h"

#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

struct Checksum : public llvm::PassInfoMixin<Checksum> {
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &);

  void link_with_hash(llvm::Module &M, std::string&) noexcept;
};

#endif // LLVM_PUF_CHECKSUM_H
