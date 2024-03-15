#ifndef LLVM_PUF_PATCHER_CROSSOVER_H
#define LLVM_PUF_PATCHER_CROSSOVER_H

#include <vector>
#include "json.hh"

namespace crossover {
    struct MetadataRequest {
        uint64_t constant;
        std::string function;
    };

    inline void to_json(nlohmann::json &j, const MetadataRequest &hr) {
        j = nlohmann::json{{"constant", hr.constant},
                           {"function", hr.function}};
    }

    struct Input {
        std::vector<MetadataRequest> function_metadata;
    };

    inline void to_json(nlohmann::json &j, const Input &input) {
        j = nlohmann::json{
                {"function_metadata", input.function_metadata},
        };
    }

    struct FunctionBase {
        std::string function;
        uint64_t offset = 0;
    };

    inline void from_json(const nlohmann::json &j, FunctionBase &e) {
        j.at("function").get_to(e.function);
        j.at("offset").get_to(e.offset);
    }

    struct Function {
        FunctionBase base;
        uint64_t hash = 0;
        uint64_t constant = 0;
        size_t instruction_count = 0;
        std::vector<uint32_t> instructions;
    };

    inline void from_json(const nlohmann::json &j, Function &e) {
        j.at("base").get_to(e.base);
        j.at("hash").get_to(e.hash);
        j.at("constant").get_to(e.constant);
        j.at("instruction_count").get_to(e.instruction_count);
        j.at("instructions").get_to(e.instructions);
    }

    struct ElfData {
        std::vector<Function> metadata;
        std::vector<FunctionBase> offsets;
    };

    inline void from_json(const nlohmann::json &j, ElfData &e) {
        j.at("metadata").get_to(e.metadata);
        j.at("offsets").get_to(e.offsets);
    }

    struct Enrollment {
        int32_t decay_time{};
        std::vector<uint32_t> pointers;
        uint32_t auth_value{};
        std::vector<uint16_t> parity;
    };

    struct EnrollData {
        std::vector<Enrollment> enrollments;
        std::vector<uint32_t> requests;

        Enrollment *request_at(uint32_t request_timeout) {
            uint32_t decay_timeout = requests[request_timeout];

            for (auto &enrollment: enrollments) {
                if (enrollment.decay_time == decay_timeout) {
                    return &enrollment;
                }
            }

            return nullptr;
        }
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

    void write_func_requests(
            const std::string &outfile,
            const std::vector<std::string> &funcs,
            const std::pair<llvm::GlobalVariable *, std::map<llvm::Function *, uint32_t>> &lookup_table
    );

    std::unordered_map<std::string, crossover::Function> read_func_metadata(const std::string &infile);

    EnrollData read_enrollment_data(const std::string &file);
}

#endif //LLVM_PUF_PATCHER_CROSSOVER_H
