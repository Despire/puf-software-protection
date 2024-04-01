#ifndef LLVM_PUF_PATCHER_CROSSOVER_H
#define LLVM_PUF_PATCHER_CROSSOVER_H

#include <unordered_map>
#include <vector>
#include "json.hh"

namespace crossover {
    // -------------- Replacement ------------------------------
    struct Replacement {
        uint32_t puf_response = 0x0;
        std::string function;
        std::string take_offset_from_function;

        friend void to_json(nlohmann::json &j, const Replacement &r) {
            j = nlohmann::json{
                {"puf_response", r.puf_response},
                {"function", r.function},
                {"take_offset_from_function", r.take_offset_from_function}
            };
        }
    };

    struct ReplacementsRequest {
        std::vector<Replacement> replacements;

        friend void to_json(nlohmann::json &j, const ReplacementsRequest &r) {
            j = nlohmann::json{{"replacements", r.replacements}};
        }
    };

    // this will generate the a JSON with functions that have a known definition within the LLVM IR.
    void write_replacements_requests(const std::string &out_file, const ReplacementsRequest &funcs);

    // -------------- Enrollment Related -----------------------
    struct Enrollment {
        int32_t decay_time{};
        std::vector<uint32_t> pointers;
        uint32_t auth_value{};
        std::vector<uint16_t> parity;

        friend void from_json(const nlohmann::json &j, Enrollment &e) {
            j.at("decay_time").get_to(e.decay_time);
            j.at("pointers").get_to(e.pointers);
            j.at("auth_value").get_to(e.auth_value);
            j.at("parity").get_to(e.parity);
        }
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

        friend void from_json(const nlohmann::json &j, EnrollData &ed) {
            j.at("enrollments").get_to(ed.enrollments);
            j.at("requests").get_to(ed.requests);
            j.at("read_with_delay").get_to(ed.read_with_delay);
        }
    };

    EnrollData read_enrollment_data(const std::string &file);

    // ------------------- Write out LLVM functions with definitions -----------------
    struct MetadataRequest {
        uint64_t constant;
        std::string function;

        friend void to_json(nlohmann::json &j, const MetadataRequest &r) {
            j = nlohmann::json{{"constant", r.constant}, {"function", r.function}};
        }

        friend void from_json(const nlohmann::json &j, MetadataRequest &r) {
            j.at("constant").get_to(r.constant);
            j.at("function").get_to(r.function);
        }
    };

    struct ReadRequest {
        std::vector<MetadataRequest> function_metadata;

        friend void from_json(const nlohmann::json &j, ReadRequest &r) {
            j.at("function_metadata").get_to(r.function_metadata);
        }

        friend void to_json(nlohmann::json &j, const ReadRequest &input) {
            j = nlohmann::json{{"function_metadata", input.function_metadata}};
        }
    };
    
    // this will generate the a JSON with functions that have a known definition within the LLVM IR.
    void write_func_requests(const std::string &out_file, const std::vector<std::string> &funcs);

    // this will read out the modified functions, where functions that were present in the LLVM IR
    // are not present in the final binary (possibly due to optimization)
    std::unordered_map<std::string, crossover::MetadataRequest> read_func_response(const std::string &in_file);
}

#endif //LLVM_PUF_PATCHER_CROSSOVER_H
