#ifndef LLVM_LLVM_PUF_OPAQUEPREDICATES_H
#define LLVM_LLVM_PUF_OPAQUEPREDICATES_H

#include "llvm/Passes/PassBuilder.h"

namespace OpaquePredicates {
    // OROpaquelyTruePredicatesFuncCount represents the number of supported OR Loop Condition opaque predicate transformations.
    constexpr int OROpaquelyTruePredicatesFuncCount = 3;

    // ANDOpaquelyTruePredicatesFuncCount represents the number of supported AND Loop Condition opaque predicate transformations.
    constexpr int ANDOpaquelyTruePredicatesFuncCount = 3;

    using OpaquelyTruePredicate = llvm::Value *(*)(llvm::Value *, llvm::Instruction *);

    OpaquelyTruePredicate OROpaquelyTruePredicates[OROpaquelyTruePredicatesFuncCount];
    OpaquelyTruePredicate ANDOpaquelyTruePredicates[ANDOpaquelyTruePredicatesFuncCount];

    inline
    llvm::Value *
    conditionOpaquePredicateOR(
            llvm::Value *ChosenInteger,
            llvm::Instruction *InsertBefore
    ) {
        llvm::IRBuilder<> Builder(InsertBefore);

        // ((a & 1 == 0) || (3(x+1)(x+2)) % 2 == 0)
        llvm::Value *Res = Builder.CreateOr(
                Builder.CreateICmpEQ(
                        Builder.CreateAnd(
                                ChosenInteger,
                                LLVM_CONST_INT(ChosenInteger->getType(), 1)
                        ),
                        LLVM_CONST_INT(ChosenInteger->getType(), 0)
                ),
                Builder.CreateICmpEQ(
                        Builder.CreateSRem(
                                Builder.CreateMul(
                                        // 3(x^2 + x)
                                        Builder.CreateAdd(
                                                // x^2
                                                Builder.CreateMul(
                                                        ChosenInteger,
                                                        ChosenInteger
                                                ),
                                                ChosenInteger
                                        ),
                                        LLVM_CONST_INT(ChosenInteger->getType(), 3)
                                ),
                                LLVM_CONST_INT(ChosenInteger->getType(), 2)
                        ),
                        LLVM_CONST_INT(ChosenInteger->getType(), 0)
                )
        );

        return Res;
    }



    inline
    llvm::Value *
    conditionOpaquePredicateAND(
            llvm::Value *ChosenInteger,
            llvm::Instruction *InsertBefore
    ) {
        llvm::IRBuilder<> Builder(InsertBefore);

        // 3(x^2 + x) % 2 == 0
        llvm::Value *LHS = Builder.CreateICmpEQ(
                Builder.CreateSRem(
                        Builder.CreateMul(
                                // 3(x^2 + x)
                                Builder.CreateAdd(
                                        // x^2
                                        Builder.CreateMul(
                                                ChosenInteger,
                                                ChosenInteger
                                        ),
                                        ChosenInteger
                                ),
                                LLVM_CONST_INT(ChosenInteger->getType(), 3)
                        ),
                        LLVM_CONST_INT(ChosenInteger->getType(), 2)
                ),
                LLVM_CONST_INT(ChosenInteger->getType(), 0)
        );

        // (x^2 + x) % 2 == 0
        llvm::Value *RHS = Builder.CreateICmpEQ(
                Builder.CreateSRem(
                        // (x^2 + x)
                        Builder.CreateAdd(
                                // x^2
                                Builder.CreateMul(
                                        ChosenInteger,
                                        ChosenInteger
                                ),
                                ChosenInteger
                        ),
                        LLVM_CONST_INT(ChosenInteger->getType(), 2)
                ),
                LLVM_CONST_INT(ChosenInteger->getType(), 0)
        );

        // 3(x^2 + x) % 2 == 0 && (x^2 + x) % 2 == 0)
        return Builder.CreateAnd(LHS, RHS);
    }

    inline
    llvm::Value *
    conditionOpaquePredicateORv2(llvm::Value *ChosenInteger, llvm::Instruction *InsertBefore) {
        llvm::IRBuilder<> Builder(InsertBefore);

        // (a & 1 == 1 || (x^2 + x) % 2 == 0)
        llvm::Value *Res = Builder.CreateOr(
                Builder.CreateICmpEQ(
                        Builder.CreateAnd(
                                ChosenInteger,
                                LLVM_CONST_INT(ChosenInteger->getType(), 1)
                        ),
                        LLVM_CONST_INT(ChosenInteger->getType(), 1)
                ),
                Builder.CreateICmpEQ(
                        Builder.CreateSRem(
                                // (x^2 + x)
                                Builder.CreateAdd(
                                        // x^2
                                        Builder.CreateMul(
                                                ChosenInteger,
                                                ChosenInteger
                                        ),
                                        ChosenInteger
                                ),
                                LLVM_CONST_INT(ChosenInteger->getType(), 2)
                        ),
                        LLVM_CONST_INT(ChosenInteger->getType(), 0)
                )
        );

        return Res;
    }

    inline
    llvm::Value *
    conditionOpaquePredicateORv3(llvm::Value *ChosenInteger, llvm::Instruction *InsertBefore) {
        llvm::IRBuilder<> Builder(InsertBefore);

        // ((2x + 2)(2x) % 4 == 0 || (x^2 + x) % 2 == 0)
        llvm::Value *Res = Builder.CreateOr(
                Builder.CreateICmpEQ(
                        Builder.CreateSRem(
                                Builder.CreateMul(
                                        Builder.CreateMul(ChosenInteger, LLVM_CONST_INT(ChosenInteger->getType(), 2)),
                                        Builder.CreateAdd(
                                                LLVM_CONST_INT(ChosenInteger->getType(), 2),
                                                Builder.CreateMul(
                                                        ChosenInteger,
                                                        LLVM_CONST_INT(ChosenInteger->getType(), 2)
                                                )
                                        )
                                ),
                                LLVM_CONST_INT(ChosenInteger->getType(), 4)
                        ),
                        LLVM_CONST_INT(ChosenInteger->getType(), 0)
                ),
                Builder.CreateICmpEQ(
                        Builder.CreateSRem(
                                // (x^2 + x)
                                Builder.CreateAdd(
                                        // x^2
                                        Builder.CreateMul(
                                                ChosenInteger,
                                                ChosenInteger
                                        ),
                                        ChosenInteger
                                ),
                                LLVM_CONST_INT(ChosenInteger->getType(), 2)
                        ),
                        LLVM_CONST_INT(ChosenInteger->getType(), 0)
                )
        );

        return Res;
    }

    inline
    llvm::Value *
    conditionOpaquePredicateANDv2(llvm::Value *ChosenInteger, llvm::Instruction *InsertBefore) {
        llvm::IRBuilder<> Builder(InsertBefore);

        // ((x^2 + x) % 2 == 0 && ((2x+2)(2x) % 4 == 0)
        llvm::Value *Res = Builder.CreateAnd(
                Builder.CreateICmpEQ(
                        Builder.CreateSRem(
                                Builder.CreateMul(
                                        Builder.CreateMul(ChosenInteger, LLVM_CONST_INT(ChosenInteger->getType(), 2)),
                                        Builder.CreateAdd(
                                                LLVM_CONST_INT(ChosenInteger->getType(), 2),
                                                Builder.CreateMul(
                                                        ChosenInteger,
                                                        LLVM_CONST_INT(ChosenInteger->getType(), 2)
                                                )
                                        )
                                ),
                                LLVM_CONST_INT(ChosenInteger->getType(), 4)
                        ),
                        LLVM_CONST_INT(ChosenInteger->getType(), 0)
                ),
                Builder.CreateICmpEQ(
                        Builder.CreateSRem(
                                // (x^2 + x)
                                Builder.CreateAdd(
                                        // x^2
                                        Builder.CreateMul(
                                                ChosenInteger,
                                                ChosenInteger
                                        ),
                                        ChosenInteger
                                ),
                                LLVM_CONST_INT(ChosenInteger->getType(), 2)
                        ),
                        LLVM_CONST_INT(ChosenInteger->getType(), 0)
                )
        );

        return Res;
    }

    inline
    llvm::Value *
    conditionOpaquePredicateANDv3(llvm::Value *ChosenInteger, llvm::Instruction *InsertBefore) {
        llvm::IRBuilder<> Builder(InsertBefore);

        // (x + x^3) % 2 == 0 && (2x + 2)(2x) % 4 == 0)
        llvm::Value *Res = Builder.CreateAnd(
                //(2x + 2)(2x) % 4 == 0
                Builder.CreateICmpEQ(
                        Builder.CreateSRem(
                                Builder.CreateMul(
                                        Builder.CreateMul(ChosenInteger, LLVM_CONST_INT(ChosenInteger->getType(), 2)),
                                        Builder.CreateAdd(
                                                LLVM_CONST_INT(ChosenInteger->getType(), 2),
                                                Builder.CreateMul(
                                                        ChosenInteger,
                                                        LLVM_CONST_INT(ChosenInteger->getType(), 2)
                                                )
                                        )
                                ),
                                LLVM_CONST_INT(ChosenInteger->getType(), 4)
                        ),
                        LLVM_CONST_INT(ChosenInteger->getType(), 0)
                ),
                Builder.CreateICmpEQ(
                        Builder.CreateSRem(
                                // (x^3 + x)
                                Builder.CreateAdd(
                                        // x^3
                                        Builder.CreateMul(
                                                Builder.CreateMul(
                                                        ChosenInteger,
                                                        ChosenInteger
                                                ),
                                                ChosenInteger
                                        ),
                                        ChosenInteger
                                ),
                                LLVM_CONST_INT(ChosenInteger->getType(), 2)
                        ),
                        LLVM_CONST_INT(ChosenInteger->getType(), 0)
                )
        );

        return Res;
    }

    OpaquelyTruePredicate getRandomOpaquelyTruePredicate(std::mt19937_64 &rng) {
        // ((a & 1 == 0) || 3(x^2 + x) % 2 == 0)
        OROpaquelyTruePredicates[0] = &conditionOpaquePredicateOR;
        // (a & 1 == 1 || (x^2 + x) % 2 == 0)
        OROpaquelyTruePredicates[1] = &conditionOpaquePredicateORv2;
        // ((2x + 2)(2x) % 4 == 0 || (x^2 + x) % 2 == 0)
        OROpaquelyTruePredicates[2] = &conditionOpaquePredicateORv3;

        // 3(x^2 + x) % 2 == 0 && (x^2 + x) % 2 == 0)
        ANDOpaquelyTruePredicates[0] = &conditionOpaquePredicateAND;
        // ((x^2 + x) % 2 == 0 && ((2x+2)(2x) % 4 == 0)
        ANDOpaquelyTruePredicates[1] = &conditionOpaquePredicateANDv2;
        // (x + x^3) % 2 == 0 && (2x + 2)(2x) % 4 == 0)
        ANDOpaquelyTruePredicates[2] = &conditionOpaquePredicateANDv3;

        auto idx = rng();

        if (rng() % 2) {
            return OROpaquelyTruePredicates[idx % OROpaquelyTruePredicatesFuncCount];
        }
        return ANDOpaquelyTruePredicates[idx % ANDOpaquelyTruePredicatesFuncCount];
    }
}

#endif // LLVM_LLVM_PUF_OPAQUEPREDICATES_H
