#ifndef LLVM_PUF_UTILS_H
#define LLVM_PUF_UTILS_H


#include <set>
#include <random>

#include "llvm/IR/IRBuilder.h"
#include "llvm/Analysis/CallGraph.h"

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

#endif //LLVM_PUF_UTILS_H
