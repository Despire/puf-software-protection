#include <sstream>

#include "Checksum.h"
#include "OpaquePredicates.h"

#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"

void Checksum::run(
        llvm::Module &M,
        const std::vector<llvm::Function *> &funcs,
        const std::unordered_map<std::string, crossover::FunctionInfo> &table
) {
    bool emptyPatch = table.empty();

    auto &ctx = M.getContext();
    for (auto &func: funcs) {
        if (!emptyPatch) { // each function we are going to patch should lbe in the table.
            assert(table.find(func->getName().str()) != table.end());
        }
        std::vector<llvm::Function *> targetFuncs = {
                func,
        }; // TODO: correctly pick target functions. also the target functions should be in the table.

        patch_function(
                ctx,
                emptyPatch,
                M,
                *func,
                funcs,
                targetFuncs,
                table
        );
    }
}

void
Checksum::patch_function(
        llvm::LLVMContext &ctx,
        bool emptyPatch,
        llvm::Module &M,
        llvm::Function &F,
        const std::vector<llvm::Function *> &allFuncs,
        const std::vector<llvm::Function *> &targetFuncs,
        const std::unordered_map<std::string, crossover::FunctionInfo> &allFuncMetadata
) noexcept {
    if (F.getName().str() == "__rust_alloc_error_handler") {
        llvm::outs() << "adding checksum to: " << F.getName().str() << "\n";
    }
    uint32_t seed = std::accumulate(F.getName().begin(), F.getName().end(), 0);
    auto rng = RandomRNG(seed);

    llvm::IRBuilder<> builder(&*F.getEntryBlock().getFirstInsertionPt());

    auto split_block = add_checksum(ctx, emptyPatch, M, &F, targetFuncs, allFuncMetadata);
    auto split_instruction = split_block->getTerminator();

    llvm::Value *RandomFunc = *RandomElementRNG(allFuncs.begin(), allFuncs.end(), rng);

    builder.SetInsertPoint(split_instruction);
    llvm::Value *RandomInteger = builder.CreatePtrToInt(RandomFunc, LLVM_I64(ctx));

    auto *predicate = OpaquePredicates::getRandomOpaquelyTruePredicate(rng);
    llvm::Value *condition = (&*predicate)(RandomInteger, split_instruction);
    auto newBB = llvm::SplitBlockAndInsertIfThen(condition, split_instruction, false);

    // since we're using a true predicate swap the branch instruction successor.
    llvm::cast<llvm::BranchInst>(split_block->getTerminator())->swapSuccessors();

    llvm::InlineAsm *parity = llvm::InlineAsm::get(
            llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), false),
            PARITY_INSTRUCTION,
            "",
            true
    );

    builder.SetInsertPoint(newBB);
    builder.CreateCall(parity, {});
}

llvm::BasicBlock *
Checksum::add_checksum(
        llvm::LLVMContext &ctx,
        bool emptyPatch,
        llvm::Module &M,
        llvm::Function *function,
        const std::vector<llvm::Function *> &targetFuncs,
        const std::unordered_map<std::string, crossover::FunctionInfo> &allFuncsMetadata
) noexcept {
    auto &function_entry_block = function->getEntryBlock();

    auto new_entry_block = llvm::BasicBlock::Create(
            ctx,
            "new_entry_block",
            function,
            &function_entry_block
    );

    llvm::IRBuilder<> Builder(new_entry_block);
    Builder.CreateBr(&function_entry_block);

    for (auto &targetFunc: targetFuncs) {
        Builder.SetInsertPoint(&*new_entry_block->getFirstInsertionPt());
        llvm::Value *call = nullptr;
        {
            if (!emptyPatch) {
                auto metadata = allFuncsMetadata.at(function->getName().str());
                auto cast = Builder.CreatePtrToInt(targetFunc, LLVM_I64(ctx));
                call = Builder.CreateCall(checksum(ctx), {
                        LLVM_CONST_I32(ctx, metadata.constant),
                        LLVM_CONST_I32(ctx, 0), // accumulator
                        cast,
                        LLVM_CONST_I32(ctx, metadata.instruction_count)
                });
            } else {
                auto cast = Builder.CreatePtrToInt(targetFunc, LLVM_I64(ctx));
                call = Builder.CreateCall(checksum(ctx), {
                        LLVM_CONST_I32(ctx, 1),
                        LLVM_CONST_I32(ctx, 0), // accumulator
                        cast,
                        LLVM_CONST_I32(ctx, 2),
                });
            }
        }

        auto *checksum = Builder.CreateExtractValue(call, {0});
        auto cond = Builder.CreateICmpNE(checksum, LLVM_CONST_I32(ctx, 0));
        auto newBB = llvm::SplitBlockAndInsertIfThen(cond, new_entry_block->getTerminator(), false);

        // add exit call for now.
        auto *ftype = llvm::FunctionType::get(
                llvm::Type::getVoidTy(ctx),
                LLVM_I32(ctx),
                false
        );

        auto exitf = M.getOrInsertFunction("exit", ftype);
        Builder.SetInsertPoint(newBB);
        Builder.CreateCall(exitf, {LLVM_CONST_I32(ctx, CKS_FAIL)});
    }

    return new_entry_block;
}

llvm::InlineAsm *Checksum::checksum(llvm::LLVMContext &ctx) noexcept {
    auto typ = llvm::FunctionType::get(
            llvm::StructType::get(LLVM_I32(ctx), LLVM_I64(ctx), LLVM_I32(ctx)),
            {LLVM_I32(ctx), LLVM_I32(ctx), LLVM_I64(ctx), LLVM_I32(ctx)},
            false
    );

    std::string checksum = R"(1:
ldr r3, [$1], #4
rev r3, r3
mul $0, $3, $0
add $0, $0, r3
subs $2, $2, #1
bne 1b
)";

    return llvm::InlineAsm::get(
            typ,
            checksum,
            "=r,=r,=r,r,0,1,2,~{r3},~{cc},~{memory}",
            true
    );
}