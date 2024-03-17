#ifndef LLVM_PUF_CHECKSUM_H
#define LLVM_PUF_CHECKSUM_H

#include <unordered_map>

#include "Utils.h"
#include "Crossover.h"

#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

#define PARITY_INSTRUCTION ".byte 00, 01, 02, 03"
#define PARITY_INSTRUCTION_INT 66051

struct Checksum {
public:
    void run(
            llvm::Module &M,
            const std::vector<llvm::Function *> &funcs,
            const std::unordered_map<std::string, crossover::FunctionInfo> &table
    );

private:
    llvm::InlineAsm *checksum(llvm::LLVMContext &ctx) noexcept;

    llvm::BasicBlock* add_checksum(
            llvm::LLVMContext &ctx,
            bool emptyPatch,
            llvm::Module &M,
            llvm::Function *function,
            const std::vector<llvm::Function *> &targetFuncs,
            const std::unordered_map<std::string, crossover::FunctionInfo> &allFuncsMetadata
    ) noexcept;

    void patch_function(
            llvm::LLVMContext &ctx,
            bool emptyPatch,
            llvm::Module &M,
            llvm::Function &F,
            const std::vector<llvm::Function *> &allFuncs,
            const std::vector<llvm::Function *> &targetFuncs,
            const std::unordered_map<std::string, crossover::FunctionInfo> &allFuncMetadata
    ) noexcept;
};

#endif // LLVM_PUF_CHECKSUM_H
