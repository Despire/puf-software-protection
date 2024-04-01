#ifndef LLVM_PUF_UTILS_H
#define LLVM_PUF_UTILS_H

#include <set>
#include <random>

#include <llvm/Transforms/Utils/ModuleUtils.h>
#include "llvm/IR/IRBuilder.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/IR/InlineAsm.h"

// Placeholders for inline assembly that will
// be patched in the binary. We need placeholders
// as otherwise the code when compiled will result
// in different instructions and we need a stable
// reproducible build.
#define START_ADDR          0xBBAADDE1
#define INSTRUCTION_COUNT   0xBBAADDE2
#define CONSTANT_MULTIPLIER 0xBBAADDE3

// Placeholder for inline assembly that will
// be patched in the binary.
#define REFERENCE_VALUE_MARK 0xBBAADDE4

#define LLVM_I8(ctx)        llvm::IntegerType::getInt8Ty(ctx)
#define LLVM_I16(ctx)       llvm::IntegerType::getInt16Ty(ctx)
#define LLVM_I32(ctx)       llvm::IntegerType::getInt32Ty(ctx)
#define LLVM_U32(ctx)       llvm::IntegerType::get(ctx, 32)
#define LLVM_I64(ctx)       llvm::IntegerType::getInt64Ty(ctx)
#define LLVM_CONST_I32(ctx, val) llvm::ConstantInt::get(LLVM_I32(ctx), val)
#define LLVM_CONST_INT(typ, val) llvm::ConstantInt::get(typ, val)

// Custom return value when the device fails to open.
#define DEV_FAIL 0x9c
#define CKS_FAIL 0x9E

inline std::mt19937_64 RandomRNG(uint32_t seed = 0x42) {
    return std::mt19937_64(seed);
}

template<typename Iter, typename RNG>
inline Iter RandomElementRNG(Iter start, Iter end, RNG &&rng) {
    std::uniform_int_distribution<> dis(0, std::distance(start, end) - 1);
    std::advance(start, dis(rng));
    return start;
}

template<typename RNG>
inline int32_t random_i32(int32_t max, RNG &&rng) { return rng() % max; }

inline void fill_32bits(uint32_t *index, uint8_t *array, uint32_t ptr) {
    array[*index] = (ptr >> 24) & 0xFF;
    array[(*index) + 1] = (ptr >> 16) & 0xFF;
    array[(*index) + 2] = (ptr >> 8) & 0xFF;
    array[(*index) + 3] = ptr & 0xFF;
    *index += 4;
}

inline void fill_16bits(uint32_t *index, uint8_t *array, uint16_t value) {
    uint8_t first = (value >> 8) & 0xff;
    uint8_t second = value & 0xff;

    array[*index] = first;
    array[(*index) + 1] = second;
    *index += 2;
}

inline void fill_8bits(uint32_t *index, uint8_t *array, uint8_t value) {
    array[*index] = value;
    *index += 1;
}


#endif //LLVM_PUF_UTILS_H
