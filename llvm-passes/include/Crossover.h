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

    struct ReadRequest {
        std::vector<MetadataRequest> function_metadata;
    };

    inline void to_json(nlohmann::json &j, const ReadRequest &input) {
        j = nlohmann::json{{"function_metadata", input.function_metadata}};
    }

    struct FunctionBase {
        std::string function;
        uint64_t offset = 0;
    };

    inline void from_json(const nlohmann::json &j, FunctionBase &e) {
        j.at("function").get_to(e.function);
        j.at("offset").get_to(e.offset);
    }

    struct FunctionInfo {
        FunctionBase base;
        uint64_t constant = 0;
        size_t instruction_count = 0;
    };

    inline void from_json(const nlohmann::json &j, FunctionInfo &e) {
        j.at("base").get_to(e.base);
        j.at("constant").get_to(e.constant);
        j.at("instruction_count").get_to(e.instruction_count);
    }

    struct ReadRequestResponse {
        std::vector<FunctionInfo> function_metadata;
    };

    inline void from_json(const nlohmann::json &j, ReadRequestResponse &e) {
        j.at("function_metadata").get_to(e.function_metadata);
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
        uint32_t read_with_delay;

        [[nodiscard]] const Enrollment *request_at(uint32_t request_timeout) const {
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
        j.at("read_with_delay").get_to(ed.read_with_delay);
    }

    void write_func_requests(
            const std::string &outfile,
            const std::vector<std::string> &funcs
    );

    std::unordered_map<std::string, crossover::FunctionInfo> read_func_metadata(const std::string &infile);

    EnrollData read_enrollment_data(const std::string &file);
}

#endif //LLVM_PUF_PATCHER_CROSSOVER_H
