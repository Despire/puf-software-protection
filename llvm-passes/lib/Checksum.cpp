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
    auto &ctx = M.getContext();
    for (auto &func: funcs) {
        assert(table.find(func->getName().str()) != table.end());
        std::vector<llvm::Function *> targetFuncs = {
                func,
        }; // TODO: correctly pick target functions. also the target functions should be in the table.

        patch_function(
                ctx,
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
        llvm::Module &M,
        llvm::Function &F,
        const std::vector<llvm::Function *> &allFuncs,
        const std::vector<llvm::Function *> &targetFuncs,
        const std::unordered_map<std::string, crossover::FunctionInfo> &allFuncMetadata
) noexcept {
    uint32_t seed = std::accumulate(F.getName().begin(), F.getName().end(), 0);
    auto rng = RandomRNG(seed);

    auto split_block = add_checksum(ctx, M, &F, targetFuncs, allFuncMetadata);
    auto split_instruction = split_block->getTerminator();

    llvm::Value *RandomFunc = *RandomElementRNG(allFuncs.begin(), allFuncs.end(), rng);

    llvm::IRBuilder<> builder(split_instruction);
    llvm::Value *RandomInteger = builder.CreatePtrToInt(RandomFunc, LLVM_I64(ctx));

    auto *predicate = OpaquePredicates::getRandomOpaquelyTruePredicate(rng);
    llvm::Value *condition = (&*predicate)(RandomInteger, split_instruction);
    auto newBB = llvm::SplitBlockAndInsertIfThen(condition, split_instruction, false);

    // since we're using a true predicate swap the branch instruction successor.
    llvm::cast<llvm::BranchInst>(split_block->getTerminator())->swapSuccessors();

    llvm::InlineAsm *parity = llvm::InlineAsm::get(
            llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), false),
            PARITY_INSTRUCTION,
            "~{memory}",
            true
    );

    builder.SetInsertPoint(newBB);
    builder.CreateCall(parity, {});
}

llvm::BasicBlock *
Checksum::add_checksum(
        llvm::LLVMContext &ctx,
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

    Builder.SetInsertPoint(&*new_entry_block->getFirstInsertionPt());
    auto *checksum_ptr = Builder.CreateAlloca(LLVM_I32(ctx));
    Builder.CreateStore(LLVM_CONST_I32(ctx, 0), checksum_ptr);
    // bounds will be patched in the elf directly.
    for (auto &_: targetFuncs) {
        Builder.CreateCall(generate_checksum_func_with_asm(M), {checksum_ptr});
    }
    auto cond = Builder.CreateICmpNE(Builder.CreateLoad(LLVM_I32(ctx), checksum_ptr), LLVM_CONST_I32(ctx, 0));
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

    return new_entry_block;
}

llvm::Function *Checksum::generate_checksum_func_with_asm(llvm::Module &M) {
    static int i = 0;
    auto &ctx = M.getContext();

    std::string function_name = "s" + std::to_string(i++);
    std::string address_label_name = function_name + "0";
    std::string count_label_name = function_name + "1";
    std::string constant_label_name = function_name + "2";

    llvm::Function *address_func = llvm::Function::Create(
            llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), false),
            llvm::Function::LinkageTypes::InternalLinkage,
            "a" + function_name,
            M
    );

    address_func->addFnAttr(llvm::Attribute::NoInline);
    address_func->addFnAttr(llvm::Attribute::OptimizeNone);

    llvm::BasicBlock *entry_block = llvm::BasicBlock::Create(
            address_func->getContext(),
            "",
            address_func
    );

    llvm::IRBuilder<> Builder(entry_block);

    // create labels
    auto *address_label_global = new llvm::GlobalVariable(
            M,
            llvm::ArrayType::get(LLVM_I8(ctx), 0),
            false,
            llvm::GlobalValue::LinkageTypes::ExternalLinkage,
            nullptr,
            address_label_name
    );
    auto *count_label_global = new llvm::GlobalVariable(
            M,
            llvm::ArrayType::get(LLVM_I8(ctx), 0),
            false,
            llvm::GlobalValue::LinkageTypes::ExternalLinkage,
            nullptr,
            count_label_name
    );
    auto *constant_label_global = new llvm::GlobalVariable(
            M,
            llvm::ArrayType::get(LLVM_I8(ctx), 0),
            false,
            llvm::GlobalValue::LinkageTypes::ExternalLinkage,
            nullptr,
            constant_label_name
    );
    auto label_addresses_global = new llvm::GlobalVariable(
            M,
            llvm::ArrayType::get(llvm::PointerType::getInt8PtrTy(ctx), 3),
            false,
            llvm::GlobalValue::LinkageTypes::InternalLinkage,
            llvm::ConstantArray::get(
                    llvm::ArrayType::get(llvm::PointerType::getInt8PtrTy(ctx), 3),
                    {address_label_global, count_label_global, constant_label_global}
            )
    );
    label_addresses_global->setDSOLocal(true);
    label_addresses_global->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Local);

    Builder.CreateCall(
            llvm::InlineAsm::get(
                    llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), false),
                    address_label_name + ":" + ".word " + std::to_string(START_ADDR),
                    "",
                    true
            ),
            {}
    );
    Builder.CreateCall(
            llvm::InlineAsm::get(
                    llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), false),
                    count_label_name + ":" + ".word " + std::to_string(INSTRUCTION_COUNT),
                    "",
                    true
            ),
            {}
    );
    Builder.CreateCall(
            llvm::InlineAsm::get(
                    llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), false),
                    constant_label_name + ":" + ".word " + std::to_string(CONSTANT_MULTIPLIER),
                    "",
                    true
            ),
            {}
    );
    Builder.CreateRetVoid();

    llvm::Function *checksum_func = llvm::Function::Create(
            llvm::FunctionType::get(
                    llvm::Type::getVoidTy(ctx),
                    {
                            llvm::PointerType::getInt8PtrTy(ctx), // where to store the result
                    },
                    false
            ),
            llvm::Function::LinkageTypes::InternalLinkage,
            function_name,
            M
    );

    checksum_func->addFnAttr(llvm::Attribute::NoInline);
    checksum_func->addFnAttr(llvm::Attribute::OptimizeNone);

    entry_block = llvm::BasicBlock::Create(
            checksum_func->getContext(),
            "",
            checksum_func
    );
    Builder.SetInsertPoint(entry_block);

    auto *loop_header = llvm::BasicBlock::Create(ctx, "loop_header", checksum_func);
    auto *loop_body = llvm::BasicBlock::Create(ctx, "loop_body", checksum_func);
    auto *loop_footer = llvm::BasicBlock::Create(ctx, "loop_footer", checksum_func);
    auto *exit_block = llvm::BasicBlock::Create(ctx, "exit_block", checksum_func);

    auto constant_m_ptr = Builder.CreateAlloca(LLVM_I32(ctx));
    auto *checksum_ptr = Builder.CreateAlloca(LLVM_I32(ctx));
    auto *iterator_ptr = Builder.CreateAlloca(LLVM_I32(ctx));
    auto *memory_ptr = Builder.CreateAlloca(LLVM_I32(ctx));

    Builder.CreateStore(
            Builder.CreateLoad(
                    LLVM_I32(ctx),
                    Builder.CreateLoad(
                            llvm::PointerType::getInt8PtrTy(ctx),
                            Builder.CreateInBoundsGEP(
                                    label_addresses_global->getValueType(),
                                    label_addresses_global,
                                    {
                                            LLVM_CONST_I32(ctx, 0),
                                            LLVM_CONST_I32(ctx, 1)
                                    }
                            )
                    )
            ),
            iterator_ptr
    );

    Builder.CreateStore(
            Builder.CreateLoad(
                    LLVM_I32(ctx),
                    Builder.CreateLoad(
                            llvm::PointerType::getInt8PtrTy(ctx),
                            Builder.CreateInBoundsGEP(
                                    label_addresses_global->getValueType(),
                                    label_addresses_global,
                                    {
                                            LLVM_CONST_I32(ctx, 0),
                                            LLVM_CONST_I32(ctx, 0)
                                    }
                            )
                    )
            ),
            memory_ptr
    );
    Builder.CreateStore(
            Builder.CreateLoad(
                    LLVM_I32(ctx),
                    Builder.CreateLoad(
                            llvm::PointerType::getInt8PtrTy(ctx),
                            Builder.CreateInBoundsGEP(
                                    label_addresses_global->getValueType(),
                                    label_addresses_global,
                                    {
                                            LLVM_CONST_I32(ctx, 0),
                                            LLVM_CONST_I32(ctx, 2)
                                    }
                            )
                    )
            ),
            constant_m_ptr
    );
    Builder.CreateStore(LLVM_CONST_I32(ctx, 0), checksum_ptr);

    Builder.CreateBr(loop_header);

    Builder.SetInsertPoint(loop_header);
    auto *loop_condition = Builder.CreateICmpEQ(Builder.CreateLoad(LLVM_I32(ctx), iterator_ptr),
                                                LLVM_CONST_I32(ctx, 0));
    Builder.CreateCondBr(loop_condition, exit_block, loop_body);

    Builder.SetInsertPoint(loop_body);
    auto *result_ptr = Builder.CreateAlloca(LLVM_I32(ctx));
    Builder.CreateStore(LLVM_CONST_I32(ctx, 0), result_ptr);

    auto *memory_pointer_0 = Builder.CreateIntToPtr(Builder.CreateLoad(LLVM_I32(ctx), memory_ptr),
                                                    llvm::PointerType::getInt8PtrTy(ctx));
    auto *first_byte = Builder.CreateLoad(LLVM_I8(ctx), memory_pointer_0);

    auto *memory_pointer_1 = Builder.CreateIntToPtr(
            Builder.CreateAdd(Builder.CreateLoad(LLVM_I32(ctx), memory_ptr), LLVM_CONST_I32(ctx, 1)),
            llvm::PointerType::getInt8PtrTy(ctx));
    auto *second_byte = Builder.CreateLoad(LLVM_I8(ctx), memory_pointer_1);

    auto *memory_pointer_2 = Builder.CreateIntToPtr(
            Builder.CreateAdd(Builder.CreateLoad(LLVM_I32(ctx), memory_ptr), LLVM_CONST_I32(ctx, 2)),
            llvm::PointerType::getInt8PtrTy(ctx));
    auto *third_byte = Builder.CreateLoad(LLVM_I8(ctx), memory_pointer_2);

    auto *memory_pointer_3 = Builder.CreateIntToPtr(
            Builder.CreateAdd(Builder.CreateLoad(LLVM_I32(ctx), memory_ptr), LLVM_CONST_I32(ctx, 3)),
            llvm::PointerType::getInt8PtrTy(ctx));
    auto *fourth_byte = Builder.CreateLoad(LLVM_I8(ctx), memory_pointer_3);

    // Construct big endian byte
    Builder.CreateStore(Builder.CreateShl(Builder.CreateZExt(first_byte, LLVM_I32(ctx)), 24), result_ptr);
    Builder.CreateStore(Builder.CreateOr(Builder.CreateLoad(LLVM_I32(ctx), result_ptr),
                                         Builder.CreateShl(Builder.CreateZExt(second_byte, LLVM_I32(ctx)), 16)),
                        result_ptr);
    Builder.CreateStore(Builder.CreateOr(Builder.CreateLoad(LLVM_I32(ctx), result_ptr),
                                         Builder.CreateShl(Builder.CreateZExt(third_byte, LLVM_I32(ctx)), 8)),
                        result_ptr);
    Builder.CreateStore(Builder.CreateOr(Builder.CreateLoad(LLVM_I32(ctx), result_ptr),
                                         Builder.CreateZExt(fourth_byte, LLVM_I32(ctx))), result_ptr);
    // add to checksum (B + H)
    Builder.CreateStore(Builder.CreateAdd(Builder.CreateLoad(LLVM_I32(ctx), checksum_ptr),
                                          Builder.CreateLoad(LLVM_I32(ctx), result_ptr)), checksum_ptr);
    // Multiply with constant C * (B + H)
    Builder.CreateStore(Builder.CreateMul(Builder.CreateLoad(LLVM_I32(ctx), checksum_ptr),
                                          Builder.CreateLoad(LLVM_I32(ctx), constant_m_ptr)), checksum_ptr);
    Builder.CreateBr(loop_footer);

    Builder.SetInsertPoint(loop_footer);
    Builder.CreateStore(Builder.CreateSub(Builder.CreateLoad(LLVM_I32(ctx), iterator_ptr), LLVM_CONST_I32(ctx, 1)),
                        iterator_ptr);
    Builder.CreateStore(Builder.CreateAdd(Builder.CreateLoad(LLVM_I32(ctx), memory_ptr), LLVM_CONST_I32(ctx, 4)),
                        memory_ptr);
    Builder.CreateBr(loop_header);

    Builder.SetInsertPoint(exit_block);
    Builder.CreateStore(
            Builder.CreateAdd(
                    Builder.CreateLoad(LLVM_I32(ctx), checksum_func->getArg(0)),
                    Builder.CreateLoad(LLVM_I32(ctx), checksum_ptr)
            ),
            checksum_func->getArg(0)
    );
    Builder.CreateRetVoid();

    llvm::appendToCompilerUsed(M, {checksum_func, address_func});
    return checksum_func;
}
