#ifndef LLVM_PUF_CHECKSUM_H
#define LLVM_PUF_CHECKSUM_H

#include <unordered_map>

#include "Utils.h"
#include "Crossover.h"

#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

#define PARITY_INSTRUCTION ".byte 00, 01, 02, 03"
#define PARITY_INSTRUCTION_INT 66051

struct Checksum : public llvm::PassInfoMixin<Checksum> {
    llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &);

    void write_func_requests(const std::string &outfile, const std::vector<std::string> &funcs) noexcept;

    std::unordered_map<std::string, Function> read_func_metadata(const std::string &infile);

    std::string calculate_parity(const Function &FuncMetadata) noexcept;

    llvm::InlineAsm *checksum(llvm::LLVMContext &ctx) noexcept;

    void add_checksum(
            llvm::LLVMContext &ctx,
            bool emptyPatch,
            llvm::Module &M,
            llvm::Function *function,
            const std::vector<llvm::Function *> &targetFuncs,
            const std::unordered_map<std::string, Function> &allFuncsMetadata
    ) noexcept;

    void patch_function(
            llvm::LLVMContext &ctx,
            bool emptyPatch,
            llvm::Module &M,
            llvm::Function &F,
            const std::vector<llvm::Function *> &allFuncs,
            const std::vector<llvm::Function *> &targetFuncs,
            const std::unordered_map<std::string, Function> &allFuncMetadata
    ) noexcept;
};

#endif // LLVM_PUF_CHECKSUM_H
