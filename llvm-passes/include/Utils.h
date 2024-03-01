#ifndef LLVM_PUF_UTILS_H
#define LLVM_PUF_UTILS_H

#include <random>

#include "llvm/IR/IRBuilder.h"

#define LLVM_I32(ctx)       llvm::IntegerType::getInt32Ty(ctx)
#define LLVM_I64(ctx)       llvm::IntegerType::getInt64Ty(ctx)
#define LLVM_CONST_I32(ctx, val) llvm::ConstantInt::get(LLVM_I32(ctx), val)
#define LLVM_CONST_INT(typ, val) llvm::ConstantInt::get(typ, val)

// Custom return value when the device fails to open.
#define DEV_FAIL 0x9c

inline std::mt19937_64 GetRandomGenerator() {
    std::random_device rd;
    std::mt19937_64 rand(rd());
    return rand;
}

inline uint64_t RandomInt64() {
    auto rand = GetRandomGenerator();
    std::uniform_int_distribution<uint64_t> dist;
    return dist(rand);
}

inline uint64_t RandomInt64(int64_t max) {
    return RandomInt64() % max;
}

inline uint64_t RandomInt64(uint64_t lo, uint64_t hi) {
    auto rand = GetRandomGenerator();
    std::uniform_int_distribution<uint64_t> dist(lo, hi - 1);
    return dist(rand);
}

template<typename Iter, typename RNG>
inline Iter RandomElementRNG(Iter start, Iter end, RNG &&rng) {
    std::uniform_int_distribution<> dis(0, std::distance(start, end) - 1);
    std::advance(start, dis(rng));
    return start;
}

template<typename Iter>
inline Iter RandomElement(Iter start, Iter end) {
    static auto rng = GetRandomGenerator();
    return RandomElementRNG(start, end, rng);
}

inline llvm::Instruction *RandomNonPHIInstruction(llvm::BasicBlock &BB) {
    int32_t count = 0;

    for (auto beg = BB.begin(); (&*beg) != BB.getFirstNonPHI(); ++beg) {
        ++count;
    }

    std::mt19937_64 gen = GetRandomGenerator();
    std::uniform_int_distribution<> dist(count, BB.size() - 1);

    auto beg = BB.begin();
    std::advance(beg, dist(gen));

    return &*beg;
}

#endif //LLVM_PUF_UTILS_H
