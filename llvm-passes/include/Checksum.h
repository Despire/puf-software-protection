#ifndef LLVM_PUF_CHECKSUM_H
#define LLVM_PUF_CHECKSUM_H

#include <unordered_map>

#include "Utils.h"

#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

#define PARITY_INSTRUCTION ".byte 00, 01, 02, 03"
#define PARITY_INSTRUCTION_INT 66051

struct Checksum {
    void run(
            llvm::Module &M,
            const std::vector<llvm::Function *> &funcs,
            llvm::GlobalVariable *puf_arr_iter_global
    );

    llvm::Function *generate_checksum_func_with_asm(llvm::Module &M);

    llvm::BasicBlock *add_checksum(
            llvm::LLVMContext &ctx,
            llvm::Module &M,
            llvm::Function *function,
            std::mt19937_64 &rng,
            llvm::GlobalVariable *puf_arr_iter_global
    ) noexcept;

    void patch_function(
            llvm::LLVMContext &ctx,
            llvm::Module &M,
            llvm::Function &F,
            const std::vector<llvm::Function *> &all_funcs,
            llvm::GlobalVariable *puf_arr_iter_global
    ) noexcept;
};

#endif // LLVM_PUF_CHECKSUM_H
