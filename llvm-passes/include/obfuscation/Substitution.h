#ifndef LLVM_PUF_PATCHER_SUBSTITUTION_H
#define LLVM_PUF_PATCHER_SUBSTITUTION_H

#include <random>
#include <numeric>
#include <algorithm>

#include "Utils.h"

#include "llvm/Passes/PassBuilder.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

namespace substitution {
    // AdditionSubstitutionCount is the number of supported addition subsitutions.
    constexpr int AdditionSubstitionFuncCount = 6;

    // SubtractionSubstitutionFuncCount is the number of supported subtraction subtitutions.
    constexpr int SubtractionSubstitutionFuncCount = 3;

    // XORSubstitutionFuncCount is the number of supported XOR subtitutions.
    constexpr int XORSubstitutionFuncCount = 4;

    // ORSubstitutionFuncCount is the number of supported OR subtitutions.
    constexpr int ORSubstitutionFuncCount = 3;

    // ANDSubstitutionFuncCount is the number of supported AND subtitutions.
    constexpr int ANDSubstitutionFuncCount = 3;

    struct Obfuscator {
        using SubstitutionHandler = bool (Obfuscator::*)(llvm::BasicBlock &, llvm::BinaryOperator *,
                                                         llvm::BasicBlock::iterator &);

        SubstitutionHandler ANDHandlers[ANDSubstitutionFuncCount];
        SubstitutionHandler ORHandlers[ORSubstitutionFuncCount];
        SubstitutionHandler XORHandlers[XORSubstitutionFuncCount];
        SubstitutionHandler SubtractHandlers[SubtractionSubstitutionFuncCount];
        SubstitutionHandler AddHandlers[AdditionSubstitionFuncCount];

        std::mt19937_64 rng;

        explicit Obfuscator(std::mt19937_64 rng) {
            Obfuscator::rng = rng;
            /*
            * Replace
            * a = b AND c
            *
            * With one of the below substitution, chosen at random (where r is a random value):
            * a = (b XOR !c) AND b
            * a = !(!b | !c) AND (r | !r)
            * a = (!b OR c) - (!b)
            */
            ANDHandlers[0] = &Obfuscator::handlerANDVersion1;
            ANDHandlers[1] = &Obfuscator::handlerANDVersion2;
            ANDHandlers[2] = &Obfuscator::handlerANDVersion3;

            /*
             * Replace
             * a = b OR c
             *
             * With one of the below substitution, chosen at random (where r is a random value):
             * a = (b & c) | (b ^ c)
             * a = [(!b & r) | (b & !r) ^ (!c & r) |(c & !r) ] | [!(!b | !c) && (r | !r)]
             * a = (b AND !c) + c
             */
            ORHandlers[0] = &Obfuscator::handlerORVersion1;
            ORHandlers[1] = &Obfuscator::handlerORVersion2;
            ORHandlers[2] = &Obfuscator::handlerORVersion3;

            /*
             * Replace
             * a = b XOR c
             *
             * With one of the below substitution, chosen at random (where r is a random value):
             * a = (b XOR r) XOR (c XOR r)
             * a = (!b AND r OR b AND !r) XOR (!c AND r OR c AND !r)
             * a = (!b AND c) OR (b AND !c)
             * a = (b OR c) - (b AND c)
             */
            XORHandlers[0] = &Obfuscator::handlerXORVersion1;
            XORHandlers[1] = &Obfuscator::handlerXORVersion2;
            XORHandlers[2] = &Obfuscator::handlerXORVersion3;
            XORHandlers[3] = &Obfuscator::handlerXORVersion4;

            /*
             * Replace
             * a = b - c
             *
             * With one of the below substitution, chosen at random (where r is a random value):
             * a = b + (-c)
             * a = b + r; a -= c; a -= r;
             * a = b - r; a -= c; a += r;
             */
            SubtractHandlers[0] = &Obfuscator::handlerSubtractVersion1;
            SubtractHandlers[1] = &Obfuscator::handlerSubtractVersion2;
            SubtractHandlers[2] = &Obfuscator::handlerSubtractVersion3;

            /*
             * Replace:
             * a = b + c
             *
             * With one of the below substitution, chosen at random (where r is a random value):
             * a = b - (-c)
             * a = -(-b + (-c))
             * a = b + r; a += c; a -=r
             * a = b - r; a += c; a += r
             * a = (b AND c) + 2 * (b XOR c)
             * a = (b AND c) + (b OR c)
             *
             * ref: https://hal.science/hal-01388109/document
             */
            AddHandlers[0] = &Obfuscator::handlerAddVersion1;
            AddHandlers[1] = &Obfuscator::handlerAddVersion2;
            AddHandlers[2] = &Obfuscator::handlerAddVersion3;
            AddHandlers[3] = &Obfuscator::handlerAddVersion4;
            AddHandlers[4] = &Obfuscator::handlerAddVersion5;
            AddHandlers[5] = &Obfuscator::handlerAddVersion6;
        }


        void run(llvm::Function &F) { for (auto &BB: F) { handleBasicBlock(BB); }}

        void handleBasicBlock(llvm::BasicBlock &BB) {
            for (auto beg = BB.begin(); beg != BB.end(); ++beg) {
                auto *BO = llvm::dyn_cast<llvm::BinaryOperator>(beg);
                if (BO == nullptr || !BO->getType()->isIntegerTy()) {
                    continue;
                }

                switch (BO->getOpcode()) {
                    case llvm::Instruction::Add: {
                        (this->*AddHandlers[random_i32(AdditionSubstitionFuncCount, rng)])(BB, BO, beg);
                        break;
                    }
                    case llvm::Instruction::Sub:
                        (this->*SubtractHandlers[random_i32(SubtractionSubstitutionFuncCount, rng)])(BB, BO, beg);
                        break;
                    case llvm::Instruction::And:
                        (this->*ANDHandlers[random_i32(ANDSubstitutionFuncCount, rng)])(BB, BO, beg);
                        break;
                    case llvm::Instruction::Or:
                        (this->*ORHandlers[random_i32(ORSubstitutionFuncCount, rng)])(BB, BO, beg);
                        break;
                    case llvm::Instruction::Xor:
                        (this->*XORHandlers[random_i32(XORSubstitutionFuncCount, rng)])(BB, BO, beg);
                        break;
                    default:
                        // Leave other operations untouched.
                        break;
                }
            }
        }

        bool handlerANDVersion1(llvm::BasicBlock &BB, llvm::BinaryOperator *BO, llvm::BasicBlock::iterator &BI) {
            llvm::IRBuilder<> builder(BO);

            // implements the substitution a = !(!b | !c) AND (r | !r)
            auto *randomConstant = llvm::ConstantInt::get(BO->getType(), random_i32(std::numeric_limits<uint32_t>::max(), rng));
            llvm::Instruction *NewInstruction = llvm::BinaryOperator::CreateAnd(
                    builder.CreateNot(
                            builder.CreateOr(
                                    builder.CreateNot(BO->getOperand(0)),
                                    builder.CreateNot(BO->getOperand(1))
                            )
                    ),
                    builder.CreateOr(
                            randomConstant,
                            builder.CreateNot(randomConstant)
                    )
            );

            llvm::ReplaceInstWithInst(&BB, BI, NewInstruction);
            return true;
        }

        bool handlerANDVersion2(llvm::BasicBlock &BB, llvm::BinaryOperator *BO, llvm::BasicBlock::iterator &BI) {
            llvm::IRBuilder<> builder(BO);

            // implements the subsitution a = (b XOR !c) AND b
            llvm::Instruction *NewInstruction = llvm::BinaryOperator::CreateAnd(
                    builder.CreateXor(
                            BO->getOperand(0),
                            builder.CreateNot(BO->getOperand(1))
                    ),
                    BO->getOperand(0)
            );

            llvm::ReplaceInstWithInst(&BB, BI, NewInstruction);
            return true;
        }

        bool handlerORVersion1(llvm::BasicBlock &BB, llvm::BinaryOperator *BO, llvm::BasicBlock::iterator &BI) {
            llvm::IRBuilder<> builder(BO);

            // implements a = [(!b & r) | (b & !r) ^ (!c & r) | (c & !r) ] | [!(!b | !c) & (r | !r)]
            auto *randomConstant = llvm::ConstantInt::get(BO->getType(), random_i32(std::numeric_limits<uint32_t>::max(), rng));
            llvm::Instruction *NewInstruction = llvm::BinaryOperator::CreateOr(
                    builder.CreateXor(
                            builder.CreateOr(
                                    builder.CreateAnd(builder.CreateNot(BO->getOperand(0)), randomConstant),
                                    builder.CreateAnd(BO->getOperand(0), builder.CreateNot(randomConstant))
                            ),
                            builder.CreateOr(
                                    builder.CreateAnd(builder.CreateNot(BO->getOperand(1)), randomConstant),
                                    builder.CreateAnd(BO->getOperand(1), builder.CreateNot(randomConstant))
                            )
                    ),
                    builder.CreateAnd(
                            builder.CreateNot(
                                    builder.CreateOr(
                                            builder.CreateNot(BO->getOperand(0)),
                                            builder.CreateNot(BO->getOperand(1))
                                    )
                            ),
                            builder.CreateOr(randomConstant, builder.CreateNot(randomConstant))
                    )
            );

            llvm::ReplaceInstWithInst(&BB, BI, NewInstruction);
            return true;
        }

        bool handlerORVersion2(llvm::BasicBlock &BB, llvm::BinaryOperator *BO, llvm::BasicBlock::iterator &BI) {
            llvm::IRBuilder<> builder(BO);

            // implements the substitution a = (b & c) | (b ^ c)
            llvm::Instruction *NewInstruction = llvm::BinaryOperator::CreateOr(
                    builder.CreateAnd(BO->getOperand(0), BO->getOperand(1)),
                    builder.CreateXor(BO->getOperand(0), BO->getOperand(1))
            );

            llvm::ReplaceInstWithInst(&BB, BI, NewInstruction);
            return true;
        }

        bool handlerXORVersion1(llvm::BasicBlock &BB, llvm::BinaryOperator *BO, llvm::BasicBlock::iterator &BI) {
            llvm::IRBuilder<> builder(BO);

            // implements a = (b XOR r) XOR (c XOR r)
            auto *randomConstant = llvm::ConstantInt::get(BO->getType(), random_i32(std::numeric_limits<uint32_t>::max(), rng));
            llvm::Instruction *NewInstruction = llvm::BinaryOperator::CreateXor(
                    builder.CreateXor(BO->getOperand(0), randomConstant),
                    builder.CreateXor(BO->getOperand(1), randomConstant)
            );

            llvm::ReplaceInstWithInst(&BB, BI, NewInstruction);
            return true;
        }

        bool handlerXORVersion2(llvm::BasicBlock &BB, llvm::BinaryOperator *BO, llvm::BasicBlock::iterator &BI) {
            llvm::IRBuilder<> builder(BO);

            // implements a = (!b AND r OR b AND !r) XOR (!c AND r OR c AND !r)
            auto *randomConstant = llvm::ConstantInt::get(BO->getType(), random_i32(std::numeric_limits<uint32_t>::max(), rng));
            llvm::Instruction *NewInstruction = llvm::BinaryOperator::CreateXor(
                    builder.CreateOr(
                            builder.CreateAnd(builder.CreateNot(BO->getOperand(0)), randomConstant),
                            builder.CreateAnd(BO->getOperand(0), builder.CreateNot(randomConstant))
                    ),
                    builder.CreateOr(
                            builder.CreateAnd(builder.CreateNot(BO->getOperand(1)), randomConstant),
                            builder.CreateAnd(BO->getOperand(1), builder.CreateNot(randomConstant))
                    )
            );

            llvm::ReplaceInstWithInst(&BB, BI, NewInstruction);
            return true;
        }

        bool handlerXORVersion3(llvm::BasicBlock &BB, llvm::BinaryOperator *BO, llvm::BasicBlock::iterator &BI) {
            llvm::IRBuilder<> builder(BO);

            // implements a = (!b AND c) OR (b AND !c)
            llvm::Instruction *NewInstruction = llvm::BinaryOperator::CreateOr(
                    builder.CreateAnd(builder.CreateNot(BO->getOperand(0)), BO->getOperand(1)),
                    builder.CreateAnd(BO->getOperand(0), builder.CreateNot(BO->getOperand(1)))
            );

            llvm::ReplaceInstWithInst(&BB, BI, NewInstruction);
            return true;
        }

        bool handlerSubtractVersion1(llvm::BasicBlock &BB, llvm::BinaryOperator *BO, llvm::BasicBlock::iterator &BI) {
            llvm::IRBuilder<> builder(BO);
            // implements a = b + (-c)
            llvm::Instruction *NewInstruction = llvm::BinaryOperator::CreateAdd(BO->getOperand(0),
                                                                                builder.CreateNeg(BO->getOperand(1)));
            llvm::ReplaceInstWithInst(&BB, BI, NewInstruction);
            return true;
        }

        bool handlerSubtractVersion2(llvm::BasicBlock &BB, llvm::BinaryOperator *BO, llvm::BasicBlock::iterator &BI) {
            llvm::IRBuilder<> builder(BO);

            // implements a = b + r; a -= c; a -= r;
            auto *randomConstant = llvm::ConstantInt::get(BO->getType(), random_i32(std::numeric_limits<uint32_t>::max(), rng));
            llvm::Instruction *NewInstruction = llvm::BinaryOperator::CreateSub(
                    builder.CreateSub(
                            builder.CreateAdd(BO->getOperand(0), randomConstant),
                            BO->getOperand(1)
                    ),
                    randomConstant
            );

            llvm::ReplaceInstWithInst(&BB, BI, NewInstruction);
            return true;
        }

        bool handlerSubtractVersion3(llvm::BasicBlock &BB, llvm::BinaryOperator *BO, llvm::BasicBlock::iterator &BI) {
            llvm::IRBuilder<> builder(BO);

            // implemenets a = b - r; a -= c; a += r;
            auto *randomConstant = llvm::ConstantInt::get(BO->getType(), random_i32(std::numeric_limits<uint32_t>::max(), rng));
            llvm::Instruction *NewInstruction = llvm::BinaryOperator::CreateAdd(
                    builder.CreateSub(
                            builder.CreateSub(BO->getOperand(0), randomConstant),
                            BO->getOperand(1)
                    ),
                    randomConstant
            );

            llvm::ReplaceInstWithInst(&BB, BI, NewInstruction);
            return true;
        }

        bool handlerAddVersion1(llvm::BasicBlock &BB, llvm::BinaryOperator *BO, llvm::BasicBlock::iterator &BI) {
            llvm::IRBuilder<> builder(BO);
            // implements a = b - (-c)
            llvm::Instruction *NewInstruction = llvm::BinaryOperator::CreateSub(BO->getOperand(0),
                                                                                builder.CreateNeg(BO->getOperand(1)));
            llvm::ReplaceInstWithInst(&BB, BI, NewInstruction);
            return true;
        }

        bool handlerAddVersion2(llvm::BasicBlock &BB, llvm::BinaryOperator *BO, llvm::BasicBlock::iterator &BI) {
            llvm::IRBuilder<> builder(BO);
            // implements a = -(-b + (-c))
            llvm::Instruction *NewInstruction = llvm::BinaryOperator::CreateNeg(
                    builder.CreateAdd(
                            builder.CreateNeg(BO->getOperand(0)),
                            builder.CreateNeg(BO->getOperand(1))
                    )
            );
            llvm::ReplaceInstWithInst(&BB, BI, NewInstruction);
            return true;
        }

        bool handlerAddVersion3(llvm::BasicBlock &BB, llvm::BinaryOperator *BO, llvm::BasicBlock::iterator &BI) {
            llvm::IRBuilder<> builder(BO);
            // implements a = b + r; a += c; a -=r
            auto *randomConstant = llvm::ConstantInt::get(BO->getType(), random_i32(std::numeric_limits<uint32_t>::max(), rng));
            llvm::Instruction *NewInstruction = llvm::BinaryOperator::CreateSub(
                    builder.CreateAdd(
                            builder.CreateAdd(BO->getOperand(0), randomConstant),
                            BO->getOperand(1)
                    ),
                    randomConstant
            );
            llvm::ReplaceInstWithInst(&BB, BI, NewInstruction);
            return true;
        }

        bool handlerAddVersion4(llvm::BasicBlock &BB, llvm::BinaryOperator *BO, llvm::BasicBlock::iterator &BI) {
            llvm::IRBuilder<> builder(BO);
            // implements a = b - r; a += c; a += r
            auto *randomConstant = llvm::ConstantInt::get(BO->getType(), random_i32(std::numeric_limits<uint32_t>::max(), rng));
            llvm::Instruction *NewInstruction = llvm::BinaryOperator::CreateAdd(
                    builder.CreateAdd(
                            builder.CreateSub(BO->getOperand(0), randomConstant),
                            BO->getOperand(1)
                    ),
                    randomConstant
            );
            llvm::ReplaceInstWithInst(&BB, BI, NewInstruction);
            return true;
        }

        bool handlerAddVersion5(llvm::BasicBlock &BB, llvm::BinaryOperator *BO, llvm::BasicBlock::iterator &BI) {
            llvm::IRBuilder<> builder(BO);

            // implements a = (b XOR c) + 2 * (b AND c)
            llvm::Instruction *NewInstruction = llvm::BinaryOperator::CreateAdd(
                    builder.CreateXor(
                            BO->getOperand(0),
                            BO->getOperand(1)
                    ),
                    builder.CreateMul(
                            llvm::ConstantInt::get(BO->getType(), 2),
                            builder.CreateAnd(
                                    BO->getOperand(0),
                                    BO->getOperand(1)
                            )
                    )
            );

            llvm::ReplaceInstWithInst(&BB, BI, NewInstruction);
            return true;
        }

        bool handlerAddVersion6(llvm::BasicBlock &BB, llvm::BinaryOperator *BO, llvm::BasicBlock::iterator &BI) {
            llvm::IRBuilder<> builder(BO);

            // implements a = (b AND c) + (b OR c)
            llvm::Instruction *NewInstruction = llvm::BinaryOperator::CreateAdd(
                    builder.CreateAnd(BO->getOperand(0), BO->getOperand(1)),
                    builder.CreateOr(BO->getOperand(0), BO->getOperand(1))
            );

            llvm::ReplaceInstWithInst(&BB, BI, NewInstruction);
            return true;
        }

        bool handlerXORVersion4(llvm::BasicBlock &BB, llvm::BinaryOperator *BO, llvm::BasicBlock::iterator &BI) {
            llvm::IRBuilder<> builder(BO);

            // implements a = (b OR c) - (b AND c)
            llvm::Instruction *NewInstruction = llvm::BinaryOperator::CreateSub(
                    builder.CreateOr(BO->getOperand(0), BO->getOperand(1)),
                    builder.CreateAnd(BO->getOperand(0), BO->getOperand(1))
            );

            llvm::ReplaceInstWithInst(&BB, BI, NewInstruction);
            return true;
        }

        bool handlerANDVersion3(llvm::BasicBlock &BB, llvm::BinaryOperator *BO, llvm::BasicBlock::iterator &BI) {
            llvm::IRBuilder<> builder(BO);

            // implements a = (!b OR c) - (!b)
            llvm::Instruction *NewInstruction = llvm::BinaryOperator::CreateSub(
                    builder.CreateOr(builder.CreateNot(BO->getOperand(0)), BO->getOperand(1)),
                    builder.CreateNot(BO->getOperand(0))
            );

            llvm::ReplaceInstWithInst(&BB, BI, NewInstruction);
            return true;
        }

        bool handlerORVersion3(llvm::BasicBlock &BB, llvm::BinaryOperator *BO, llvm::BasicBlock::iterator &BI) {
            llvm::IRBuilder<> builder(BO);

            // implements a = (b AND !c) + c
            llvm::Instruction *NewInstruction = llvm::BinaryOperator::CreateAdd(
                    builder.CreateAnd(BO->getOperand(0), builder.CreateNot(BO->getOperand(1))),
                    BO->getOperand(1)
            );

            llvm::ReplaceInstWithInst(&BB, BI, NewInstruction);
            return true;
        }
    };
}

#endif //LLVM_PUF_PATCHER_SUBSTITUTION_H
