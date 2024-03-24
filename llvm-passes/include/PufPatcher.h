#ifndef LLVM_PUF_PARSER_H
#define LLVM_PUF_PARSER_H

#include <vector>
#include <set>

#include "Utils.h"
#include "GraphUtils.h"
#include "Checksum.h"
#include "json.hh"

#include "obfuscation/Substitution.h"

#include "llvm/Analysis/CallGraph.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

// The binary will be loaded at memory offset 0x40000000
// one can identify this from ldd ./program
//      /lib/ld-linux-armhf.so.3 (0x40000000)
// Given that we use a custom Linker script we do not need this
// and we can directly use the offset of the functions.
#define BINARY_BASE_OFFSET 0x40000000

struct LibCDependencies {
    // External functions used within the LLVM pass.
    llvm::PointerType *printf_arg_type = nullptr;
    llvm::FunctionCallee printf_func;

    llvm::FunctionCallee sleep_func;
    llvm::FunctionCallee fflush_func;

    llvm::FunctionCallee pthread_attr_init_func;
    llvm::FunctionCallee pthread_attr_setdetachstate_func;
    llvm::FunctionCallee pthread_create_func;

    llvm::FunctionCallee open_func;
    llvm::FunctionCallee close_func;
    llvm::FunctionCallee write_func;
    llvm::FunctionCallee read_func;
    llvm::FunctionCallee exit_func;

    llvm::FunctionCallee rand_func;
};

struct GlobalVariables {
    llvm::GlobalVariable *puf_fd = nullptr;
    llvm::GlobalVariable *stdoutput = nullptr;
};

struct PufPatcher : public llvm::PassInfoMixin<PufPatcher> {
    struct FunctionCallReplacementInfo {
        llvm::Function *funcion_call_to_replace = nullptr;
        int32_t puff_arr_index = -1;
        uint32_t puff_response_at_offset = 0x0;
        uint32_t function_call_to_replace_address = 0x0;
    };

    GlobalVariables global_variables;
    LibCDependencies lib_c_dependencies;
    Checksum checksum;

    void init_deps(llvm::Module &M);

    void insert_address_calculations(
            const std::unordered_map<std::string, crossover::FunctionInfo> &compiled_functions_metadata,
            const crossover::EnrollData &enrollment,
            const std::pair<llvm::GlobalVariable *, size_t> &puf_array,
            const std::pair<llvm::GlobalVariable *, std::map<llvm::Function *, uint32_t>> &lookup_table,
            const llvm::CallGraph &call_graph,
            const std::set<llvm::Function *> &external_entry_points
    );

    llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &);

    llvm::Function *puf_open_ctor(
            llvm::Module &M,
            const crossover::EnrollData &enrollments,
            llvm::GlobalVariable *Fd
    );

    llvm::Function *puf_close_dtor(
            llvm::Module &M,
            llvm::GlobalVariable *Fd
    );

    std::pair<llvm::GlobalVariable *, std::map<llvm::Function *, uint32_t>>
    replace_calls_with_lookup_table(
            llvm::Module &M,
            const std::vector<llvm::Function *> &funcs
    );

    void spawn_puf_thread(
            llvm::Module &M,
            const std::pair<llvm::GlobalVariable *, size_t> &puf_array,
            llvm::Function *function_to_add_code,
            const crossover::EnrollData &enrollment
    );

    std::pair<llvm::GlobalVariable *, size_t> create_puf_array(
            llvm::Module &M,
            const crossover::EnrollData &
    );

    void generate_block_until_puf_response(
            const std::pair<llvm::GlobalVariable *, std::map<llvm::Function *, uint32_t>> &lookup_table,
            llvm::Function *function_to_add_code,
            const std::vector<FunctionCallReplacementInfo> &replacement_info,
            const std::pair<llvm::GlobalVariable *, size_t> &puf_array,
            const std::unordered_map<std::string, crossover::FunctionInfo> &compiled_functions_metadata
    );
};

#endif
