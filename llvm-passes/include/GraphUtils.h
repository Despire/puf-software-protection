#ifndef LLVM_PUF_PATCHER_GRAPHUTILS_H
#define LLVM_PUF_PATCHER_GRAPHUTILS_H

#include "Crossover.h"

#include <unordered_map>
#include <set>

#include "llvm/Analysis/CallGraph.h"

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
    // as they can be passed around between functions and basically be another
    // entry point into the module.
    for (auto &F: M) {
        if (F.hasAddressTaken()) {
            cg_entry_points.insert(&F);
        }
    }

    return cg_entry_points;
}

inline void internal_find_all_unique_functions_calls(
        llvm::Function *entry_point,
        const llvm::CallGraph &call_graph,
        std::set<llvm::Function *> &unique_calls,
        std::set<llvm::Function *> *exclude
) {
    auto *ep_node = call_graph[entry_point];
    assert(ep_node != nullptr);

    // guard againts recursion and already existing entry points.
    if (unique_calls.find(entry_point) != unique_calls.end()) {
        return;
    }

    // check if this sub graph include the functions as the root
    // should be skipped.
    if (exclude && exclude->find(entry_point) != exclude->end()) {
        return;
    }

    unique_calls.insert(entry_point);

    // traverse all calls made in this entry_point
    for (auto &call_node: *ep_node) {
        if (call_node.second->getFunction()) {
            internal_find_all_unique_functions_calls(
                    call_node.second->getFunction(),
                    call_graph,
                    unique_calls,
                    exclude
            );
        }
    }
}

// finds all functions calls starting from the requested entry points and
// when traversing the call graph it hits one of the excluded functions
// it will not recurse into that function. (i.e. call made in sub graphs
// of the excluded functions will not be considered).
inline std::set<llvm::Function *> find_all_unique_functions_calls(
        std::set<llvm::Function *> &entry_points,
        const llvm::CallGraph &call_graph,
        std::set<llvm::Function *> *exclude = nullptr
) {
    std::set<llvm::Function *> unique_calls;

    for (auto &ep: entry_points) {
        internal_find_all_unique_functions_calls(ep, call_graph, unique_calls, exclude);
    }

    return unique_calls;
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

inline std::pair<
std::set<llvm::Function *>,
std::vector<llvm::Function *>
> collect_unique_calls_from_functions_with_prefix(
        llvm::CallGraph &call_graph,
        const std::vector<llvm::Function *> &all_module_functions,
        const std::string &function_prefix,
        std::unordered_map<std::string, crossover::MetadataRequest> &compiled_functions,
        std::set<llvm::Function *> &all_external_entry_points
) {
    std::set<llvm::Function *> requested_entry_points;
    for (auto &f: all_module_functions) {
        if (f->getName().str().starts_with(function_prefix)) {
            requested_entry_points.insert(f);
        }
    }

    auto requested_entry_points_unique_calls = find_all_unique_functions_calls(requested_entry_points, call_graph);

    // filter out those calls that are not in the final binary.
    {
        std::set<llvm::Function *> to_erase;
        for (auto &f: requested_entry_points_unique_calls) {
            if (compiled_functions.find(f->getName().str()) == compiled_functions.end()) {
                to_erase.insert(f);
            }
        }
        for (auto &e: to_erase) {
            requested_entry_points_unique_calls.erase(e);
        }
    }

    auto all_unique_calls_excluding_requested_entry_points = find_all_unique_functions_calls(
            all_external_entry_points,
            call_graph,
            &requested_entry_points
    );

    // erase all functions that are found also in other paths no only
    // in the requested sub graphs.
    {
        std::set<llvm::Function *> to_erase;
        for (auto &fn: requested_entry_points_unique_calls) {
            if (all_unique_calls_excluding_requested_entry_points.find(fn) !=
                all_unique_calls_excluding_requested_entry_points.end()) {
                to_erase.insert(fn);
            }
        }
        for (auto &erase: to_erase) {
            requested_entry_points_unique_calls.erase(erase);
        }
    }

    return {
            requested_entry_points,
            {requested_entry_points_unique_calls.begin(), requested_entry_points_unique_calls.end()}
    };
}

#endif //LLVM_PUF_PATCHER_GRAPHUTILS_H
