#include <fstream>

#include "Utils.h"
#include "Crossover.h"

crossover::EnrollData crossover::read_enrollment_data(const std::string &file) {
    std::ifstream inputFile(file);
    if (!inputFile.is_open()) {
        throw std::runtime_error("failed to open file");
    }

    nlohmann::json j;
    inputFile >> j;

    return j.get<crossover::EnrollData>();
}

std::unordered_map<std::string, crossover::FunctionInfo> crossover::read_func_metadata(const std::string &infile) {
    std::unordered_map<std::string, crossover::FunctionInfo> table;

    if (infile.empty()) {
        return table;
    }

    std::ifstream inputFile(infile);
    if (!inputFile.is_open()) {
        throw std::runtime_error("failed to open file");
    }

    nlohmann::json j;
    inputFile >> j;

    auto elf_data = j.get<crossover::ReadRequestResponse>();

    for (auto &f: elf_data.function_metadata) {
        table[f.base.function] = f;
    }

    return table;
}

void crossover::write_func_requests(
        const std::string &outFile,
        const std::vector<std::string> &funcs
) {
    // Create an odd number
    std::vector<crossover::MetadataRequest> function_metadata;

    auto rng = RandomRNG();
    for (auto &f: funcs) {
        uint64_t odd = rng() % 21;
        odd = odd * 2 + 1;
        function_metadata.push_back({odd, f});
    }

    crossover::ReadRequest input = {
            .function_metadata=function_metadata,
    };

    nlohmann::json json = input;

    std::ofstream outputFile(outFile);
    if (outputFile.is_open()) {
        outputFile << json.dump(4);
        outputFile.close();
    } else {
        throw std::runtime_error("unable to open file for writing");
    }
}