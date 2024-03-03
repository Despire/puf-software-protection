#ifndef LLVM_PUF_PATCHER_CROSSOVER_H
#define LLVM_PUF_PATCHER_CROSSOVER_H

#include <vector>
#include "json.hh"

struct HashRequest {
    uint64_t constant;
    std::string function;
};

void to_json(nlohmann::json& j, const HashRequest& hr) {
    j = nlohmann::json{{"constant", hr.constant}, {"function", hr.function}};
}

struct Input {
    std::vector<HashRequest> requests;
};

void to_json(nlohmann::json& j, const Input& input) {
    j = nlohmann::json{{"requests", input.requests}};
}

struct Function {
    std::string function;
    uint64_t hash = 0;
    uint64_t constant = 0;
    size_t instruction_count = 0;
    std::vector<uint32_t> instructions;
};

inline void from_json(const nlohmann::json &j, Function &e) {
    j.at("function").get_to(e.function);
    j.at("hash").get_to(e.hash);
    j.at("constant").get_to(e.constant);
    j.at("instruction_count").get_to(e.instruction_count);
    j.at("instructions").get_to(e.instructions);
}

#endif //LLVM_PUF_PATCHER_CROSSOVER_H
