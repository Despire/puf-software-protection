#ifndef LLVM_PUF_PATCHER_CONTROLFLOWFLATTENING_H
#define LLVM_PUF_PATCHER_CONTROLFLOWFLATTENING_H

#include "llvm/Passes/PassBuilder.h"
#include "llvm/Transforms/Utils/Local.h"

namespace control_flow_flattening {
    inline std::vector<llvm::PHINode *> findAllPHINodes(llvm::Function &F) {
        std::vector<llvm::PHINode *> nodes;

        for (auto &BB: F) {
            for (auto &inst: BB) {
                if (llvm::isa<llvm::PHINode>(&inst)) {
                    nodes.push_back(llvm::cast<llvm::PHINode>(&inst));
                }
            }
        }

        return nodes;
    }

    inline std::vector<llvm::Instruction *> findAllInstructionUsedInMultipleBlocks(llvm::Function &F) {
        llvm::BasicBlock *EntryBasicBlock = &*F.begin();
        std::vector<llvm::Instruction *> usedOutside;

        // fixup instrunction referenced in multiple blocks.
        for (auto &BB: F) {
            for (auto &inst: BB) {
                // in the entry block there will be a bunch of stack allocation using alloca
                // that are referenced in multiple blocks thus we need to ignore those when
                // filtering.
                if (llvm::isa<llvm::AllocaInst>(&inst) && inst.getParent() == EntryBasicBlock) {
                    continue;
                }

                // check if used outside the current block.
                if (inst.isUsedOutsideOfBlock(&BB)) {
                    usedOutside.push_back(&inst);
                }
            }
        }

        return usedOutside;
    }

    inline bool jump_table(llvm::Function &F, std::mt19937_64 &rng) {
        llvm::LLVMContext &ctx = F.getContext();
        std::vector<llvm::BasicBlock *> functionBasicBlocks;

        // Collect the BasicBlocks and also check whether they throw an exception.
        // We won't do control flow flattening for exceptions for now.
        for (auto &beg: F) {
            // 1.st clear exception handling
            if (beg.isLandingPad() || llvm::isa<llvm::InvokeInst>(&beg)) {
                return false;
            }
            functionBasicBlocks.emplace_back(&beg);
        }

        llvm::BasicBlock *EntryBasicBlock = functionBasicBlocks.front();
        EntryBasicBlock->setName("entry");

        // If the EntryBasicBlock doesn't end in unconditional branch (i.e. it has multiple BasicBlocks where
        // control flow can go) we need to split the Block into two.
        if (llvm::dyn_cast<llvm::BranchInst>(EntryBasicBlock->getTerminator())) {
            auto LastInst = std::prev(EntryBasicBlock->end());
            // also take into account the `icmp` instruction that preceeds the `br` instruction.
            if (LastInst != EntryBasicBlock->begin()) {
                --LastInst;
            }

            llvm::BasicBlock *split = EntryBasicBlock->splitBasicBlock(LastInst, "EntryBasicBlockSplit");
            functionBasicBlocks.insert(std::next(functionBasicBlocks.begin()), split);
        }

        // remove the entry block from the list of function blocks.
        functionBasicBlocks.erase(functionBasicBlocks.begin());

        // Add BogusBasicBlock to add confusion.
        llvm::BasicBlock *bogusBasicBlock = llvm::BasicBlock::Create(ctx, "BogusBasciBlock", &F);
        bogusBasicBlock->moveAfter(EntryBasicBlock);

        llvm::IRBuilder<> builder(bogusBasicBlock);
        builder.CreateBr(*functionBasicBlocks.begin());

        functionBasicBlocks.insert(functionBasicBlocks.begin(), bogusBasicBlock);

        // collect the addresses of all basic blocks expect the entry.
        std::vector<llvm::BlockAddress *> blockAddresses;
        for (auto &BB: functionBasicBlocks) {
            blockAddresses.push_back(llvm::BlockAddress::get(BB));
        }

        builder.SetInsertPoint(&*EntryBasicBlock->getFirstInsertionPt());

        // Create Jump Table.
        llvm::Type *blockAddressTyp = (*blockAddresses.begin())->getType();
        llvm::ConstantInt *jumpTableSize = LLVM_CONST_I32(ctx, blockAddresses.size());
        llvm::AllocaInst *JumpTable = builder.CreateAlloca(blockAddressTyp, jumpTableSize, "JumpTable");

        // Populate the JumpTable with the label addresses
        std::unordered_map<std::string, llvm::Value *> namesToEps;
        for (std::size_t i = 0; i < blockAddresses.size(); i++) {
            llvm::ConstantInt *index = LLVM_CONST_I32(ctx, i);
            llvm::Value *EP = builder.CreateGEP(blockAddressTyp, JumpTable, index);
            builder.CreateStore(blockAddresses[i], EP);

            // store the element Ptr for later use.
            if (!blockAddresses[i]->getBasicBlock()->hasName()) {
                blockAddresses[i]->getBasicBlock()->setName(std::to_string(i));
            }
            namesToEps.insert({blockAddresses[i]->getBasicBlock()->getName().str(), EP});
        }

        // For each Basic block update the Branch instructions.
        for (auto &BB: functionBasicBlocks) {
            if (auto *ret = llvm::dyn_cast<llvm::ReturnInst>(BB->getTerminator()); ret != nullptr) {
                continue;
            }

            if (auto *br = llvm::dyn_cast<llvm::BranchInst>(BB->getTerminator()); br != nullptr &&
                                                                                  br->isConditional()) {
                llvm::BasicBlock *conditionTrueBB = br->getSuccessor(0);
                llvm::BasicBlock *conditionFalseBB = br->getSuccessor(1);

                llvm::Value *jumpAddressTrue = namesToEps.find(conditionTrueBB->getName().str())->second;
                llvm::Value *jumpAddressFalse = namesToEps.find(conditionFalseBB->getName().str())->second;

                builder.SetInsertPoint(br);
                llvm::Value *selectInst = builder.CreateSelect(
                        br->getCondition(),
                        jumpAddressTrue,
                        jumpAddressFalse,
                        "",
                        br
                );

                builder.SetInsertPoint(br);

                llvm::IndirectBrInst *ibr = builder.CreateIndirectBr(builder.CreateLoad(blockAddressTyp, selectInst));
                for (auto &BB: functionBasicBlocks) {
                    ibr->addDestination(BB);
                }

                br->eraseFromParent();

                continue;
            }

            if (auto *br = llvm::dyn_cast<llvm::BranchInst>(BB->getTerminator()); br != nullptr &&
                                                                                  br->isUnconditional()) {
                llvm::Value *jumpAddress = namesToEps.find(br->getSuccessor(0)->getName().str())->second;

                builder.SetInsertPoint(br);

                llvm::IndirectBrInst *ibr = builder.CreateIndirectBr(builder.CreateLoad(blockAddressTyp, jumpAddress));
                for (auto &BB: functionBasicBlocks) {
                    ibr->addDestination(BB);
                }

                br->eraseFromParent();

                continue;
            }
        }

        // Finish with the EntryBlock.
        builder.SetInsertPoint(&*EntryBasicBlock->getTerminator());
        llvm::Value *jumpAddres = namesToEps.find(
                EntryBasicBlock->getTerminator()->getSuccessor(0)->getName().str())->second;
        llvm::IndirectBrInst *ibr = builder.CreateIndirectBr(builder.CreateLoad(blockAddressTyp, jumpAddres));
        for (auto &BB: functionBasicBlocks) {
            ibr->addDestination(BB);
        }

        EntryBasicBlock->getTerminator()->eraseFromParent();

        // Add confusion to the bogus block.
        builder.SetInsertPoint(&*bogusBasicBlock->getFirstInsertionPt());

        // Insert Bogus operations.
        std::shuffle(blockAddresses.begin(), blockAddresses.end(), rng);
        for (std::size_t i = 0; i < blockAddresses.size(); i += 2) {
            llvm::ConstantInt *Index = LLVM_CONST_I32(ctx, i);
            llvm::Value *EP = builder.CreateGEP(blockAddressTyp, JumpTable, Index);
            builder.CreateStore(blockAddresses[i], EP);
        }

        // fixup instructions referenced in multiple blocks.
        for (auto &inst: findAllInstructionUsedInMultipleBlocks(F)) {
            llvm::DemoteRegToStack(*inst);
        }

        assert(findAllInstructionUsedInMultipleBlocks(F).empty() &&
               "leftover instruction that are used in multiple blocks");

        // fixup PHI nodes reference in basic blocks after the reconstruction
        // of the control flow.
        for (auto &phiNode: findAllPHINodes(F)) {
            llvm::DemotePHIToStack(phiNode);
        }

        assert(findAllPHINodes(F).empty() && "leftover PHI nodes in basic blocks");

        return true;
    }
}

#endif //LLVM_PUF_PATCHER_CONTROLFLOWFLATTENING_H