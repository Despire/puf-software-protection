#ifndef LLVM_PUF_PARSER_H
#define LLVM_PUF_PARSER_H

#include <vector>
#include <set>

#include "Utils.h"
#include "Checksum.h"
#include "json.hh"

#include "llvm/Analysis/CallGraph.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

namespace enrollment {
    struct Enrollment {
        int32_t decay_time{};
        std::vector<uint32_t> pointers;
        uint32_t auth_value{};
        std::vector<uint16_t> parity;
    };

    struct EnrollData {
        std::vector<Enrollment> enrollments;
        std::vector<uint32_t> requests;
    };

    static inline void from_json(const nlohmann::json &j, Enrollment &e) {
        j.at("decay_time").get_to(e.decay_time);
        j.at("pointers").get_to(e.pointers);
        j.at("auth_value").get_to(e.auth_value);
        j.at("parity").get_to(e.parity);
    }

    static inline void from_json(const nlohmann::json &j, EnrollData &ed) {
        j.at("enrollments").get_to(ed.enrollments);
        j.at("requests").get_to(ed.requests);
    }
}

struct PufPatcher : public llvm::PassInfoMixin<PufPatcher> {
    Checksum checksum;

    // puf_fd
    llvm::GlobalVariable *puf_fd = nullptr;
    // stdout.
    llvm::GlobalVariable *stdoutput = nullptr;

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

    void init_deps(llvm::Module &M);

    void insert_address_calculations(
            enrollment::EnrollData &,
            llvm::GlobalVariable *,
            std::map<llvm::Function*, uint32_t> &,
            llvm::CallGraph &
    );

    llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &);

    llvm::Function *puf_open_ctor(enrollment::EnrollData &enrollments, llvm::Module &M, llvm::GlobalVariable *Fd);

    llvm::Function *puf_close_dtor(llvm::Module &M, llvm::GlobalVariable *Fd);

    std::pair<llvm::GlobalVariable *, std::map<llvm::Function *, uint32_t>>
    replace_calls_with_lookup_table(
            llvm::Module &M,
            std::vector<llvm::Function *> &funcs
    );

    void spawn_puf_thread(llvm::Function *F, llvm::Module &M, enrollment::EnrollData &);

    static enrollment::EnrollData read_enrollment_data();
};

#endif
