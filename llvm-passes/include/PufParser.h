#ifndef LLVM_PUF_PARSER_H
#define LLVM_PUF_PARSER_H

#include "Utils.h"

#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

#include <vector>

#include "json.hh"

struct Enrollment {
    int decay_time{};
    std::vector<uint64_t> pointers;
    uint32_t auth_value{};
    std::vector<uint16_t> parity;
};

struct EnrollData {
    std::vector<Enrollment> enrollments;
    std::vector<uint32_t> requests;
};

inline void from_json(const nlohmann::json& j, Enrollment& e) {
    j.at("decay_time").get_to(e.decay_time);
    j.at("pointers").get_to(e.pointers);
    j.at("auth_value").get_to(e.auth_value);
    j.at("parity").get_to(e.parity);
}

inline void from_json(const nlohmann::json& j, EnrollData& ed) {
    j.at("enrollments").get_to(ed.enrollments);
    j.at("requests").get_to(ed.requests);
}

struct PufParser : public llvm::PassInfoMixin<PufParser> {
    llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &);
    bool puf_open_ctor(llvm::Module &M, llvm::GlobalVariable* Fd);
    bool puf_close_dtor(llvm::Module &M, llvm::GlobalVariable* Fd);
    static EnrollData read_enrollment_data();
};

#endif
