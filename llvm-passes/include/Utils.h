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

inline void eegcd(uint64_t a, uint64_t &x, uint64_t &y) {
    uint64_t b = uint64_t(std::numeric_limits<uint32_t>::max()) + 1;
    uint64_t x0 = 1, y0 = 0, x1 = 0, y1 = 1;

    while (b != 0) {
        uint64_t q = a / b;
        uint64_t temp = b;
        b = a % b;
        a = temp;

        uint64_t tempX = x0 - q * x1;
        uint64_t tempY = y0 - q * y1;

        x0 = x1;
        y0 = y1;
        x1 = tempX;
        y1 = tempY;
    }

    assert(a == 1);

    x = x0;
    y = y0;
}

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

inline std::set<llvm::Function *> external_nodes(llvm::CallGraph &g) {
    std::set<llvm::Function *> external;
    for (auto &p: g) {
        auto &node = p.second;
        if (node->getFunction() == nullptr) {
            continue;
        }

        std::vector<llvm::Function *> callees;
        for (auto &other_p: g) {
            auto &other_node = other_p.second;
            if (other_node->getFunction() == nullptr) {
                continue;
            }

            bool called = false;
            for (auto &call: *other_node) {
                if (call.second->getFunction() && call.second->getFunction() == node->getFunction()) {
                    called = true;
                }
            }

            if (called) {
                callees.push_back(other_node->getFunction());
            }
        }

        if (callees.empty()) {
            external.insert(node->getFunction());
        }
    }

    return external;
}

inline std::set<llvm::Function *> find_all_external_entry_points(llvm::Module &M, llvm::CallGraph &cg) {
    auto cg_entry_points = external_nodes(cg);
    // We also need to consider pointers to functions as entry points
    // as they can be passed around between functions and basicllay be another
    // entry point into the module.
    for (auto &F: M) {
        if (F.hasAddressTaken()) {
            cg_entry_points.insert(&F);
        }
    }

    return cg_entry_points;
}

inline void internal_find_insert_points(
        const llvm::CallGraph &call_graph,
        llvm::Function *entry_point,
        const llvm::Function *function_call,
        std::vector<llvm::Function *> &path,
        std::vector<std::vector<llvm::Function *>> &all_paths
) {
    assert(entry_point != nullptr);
    path.push_back(entry_point);

    // check for recursion.
    for (int i = 0; i < path.size() - 1; i++) {
        if (path[i] == entry_point) {
            path.pop_back();
            return;
        }
    }

    auto entry_point_node = call_graph[entry_point];
    assert(call_graph[entry_point] != nullptr);

    // Is the function_call used in this entry point ?
    for (auto &call_node: *entry_point_node) {
        if (call_node.second->getFunction() && call_node.second->getFunction() == function_call) {
            all_paths.emplace_back(path.begin(), path.end());
            path.pop_back();
            return;
        }
    }

    // if this entry point does not contain the function call
    // check all the function calls of the called functions within
    // this entry points.
    // recurse for all the function calls.
    for (auto &call_node: *entry_point_node) {
        if (call_node.second->getFunction()) {
            internal_find_insert_points(call_graph, call_node.second->getFunction(), function_call, path, all_paths);
        }
    }

    path.pop_back();
}

inline std::vector<llvm::Function *> find_insert_points(
        const llvm::CallGraph &call_graph,
        llvm::Function *entry_point,
        const llvm::Function *function_call
) {
    std::vector<llvm::Function *> path;
    std::vector<std::vector<llvm::Function *>> all_paths;

    internal_find_insert_points(call_graph, entry_point, function_call, path, all_paths);

    if (all_paths.empty()) {
        return {};
    }

    // return the shortest common path.
    size_t shortest_length = std::numeric_limits<size_t>::max();
    for (auto &path: all_paths) {
        if (path.size() < shortest_length) {
            shortest_length = path.size();
        }
    }

    int index = -1;
    for (int i = 0; i < shortest_length; ++i) {
        index++;
        const llvm::Function *current = all_paths[0][i];

        // check if all the paths at index I point to the
        // same function.
        bool equal = true;
        for (auto &path: all_paths) {
            if (path[i] != current) {
                equal = false;
                break;
            }
        }

        // the current didn't have all nodes in common so the previous
        // one had to be the one where all the nodes are pointing to the
        // same function.
        if (!equal) {
            index = i - 1;
            break;
        }
    }

    assert(index >= 0);
    auto beg = all_paths[0].begin();
    auto end = all_paths[0].begin();
    std::advance(end, index + 1); // end should point to one past the last, thus +1.

    return {beg, end};
}

inline llvm::Function *generate_checksum_func(llvm::Module &M) {
    static int i = 0;
    auto &ctx = M.getContext();
    llvm::Function *checksum_func = llvm::Function::Create(
            llvm::FunctionType::get(
                    llvm::Type::getVoidTy(ctx),
                    {
                            llvm::PointerType::getInt8PtrTy(ctx), // where to store the result
                            llvm::PointerType::getInt8PtrTy(ctx), // start
                            llvm::PointerType::getInt8PtrTy(ctx), // size
                            llvm::PointerType::getInt8PtrTy(ctx), // constant
                    },
                    false
            ),
            llvm::Function::LinkageTypes::InternalLinkage,
            "c" + std::to_string(i++),
            M
    );

    checksum_func->addFnAttr(llvm::Attribute::NoInline);
    checksum_func->addFnAttr(llvm::Attribute::OptimizeNone);

    llvm::BasicBlock *entry_block = llvm::BasicBlock::Create(
            checksum_func->getContext(),
            "",
            checksum_func
    );

    llvm::IRBuilder<> Builder(entry_block);

    auto *loop_header = llvm::BasicBlock::Create(ctx, "loop_header", checksum_func);
    auto *loop_body = llvm::BasicBlock::Create(ctx, "loop_body", checksum_func);
    auto *loop_footer = llvm::BasicBlock::Create(ctx, "loop_footer", checksum_func);
    auto *exit_block = llvm::BasicBlock::Create(ctx, "exit_block", checksum_func);

    auto constant_m_ptr = Builder.CreateAlloca(LLVM_I32(ctx));
    auto *checksum_ptr = Builder.CreateAlloca(LLVM_I32(ctx));
    auto *iterator_ptr = Builder.CreateAlloca(LLVM_I32(ctx));
    auto *memory_ptr = Builder.CreateAlloca(LLVM_I32(ctx));

    Builder.CreateStore(Builder.CreateLoad(LLVM_I32(ctx), checksum_func->getArg(2)), iterator_ptr);
    Builder.CreateStore(Builder.CreateLoad(LLVM_I32(ctx), checksum_func->getArg(1)), memory_ptr);
    Builder.CreateStore(LLVM_CONST_I32(ctx, 0), checksum_ptr);
    Builder.CreateStore(Builder.CreateLoad(LLVM_I32(ctx), checksum_func->getArg(3)), constant_m_ptr);

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

    llvm::appendToCompilerUsed(M, {checksum_func});
    return checksum_func;
}

#endif //LLVM_PUF_UTILS_H
