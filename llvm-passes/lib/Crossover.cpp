#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "Utils.h"
#include "json.hh"
#include "Crossover.h"

crossover::EnrollData crossover::read_enrollment_data(const std::string &file) {
    std::ifstream inputFile(file);
    if (file.empty()) {
        throw std::runtime_error("no file specified to read enrollment data from");
    }
    if (!inputFile.is_open()) {
        throw std::runtime_error("failed to open file");
    }

    nlohmann::json j;
    inputFile >> j;

    auto e = j.get<crossover::EnrollData>();
    std::sort(e.requests.begin(), e.requests.end());
    return e;
}

std::unordered_map<std::string, crossover::MetadataRequest> crossover::read_func_response(const std::string &in_file) {
    std::unordered_map<std::string, crossover::MetadataRequest> table;

    if (in_file.empty()) {
        return table;
    }

    std::ifstream input(in_file);
    if (!input.is_open()) {
        throw std::runtime_error("failed to open file");
    }

    nlohmann::json json;
    input >> json;

    auto functions = json.get<crossover::ReadRequest>();

    for (auto &f : functions.function_metadata) {
        if (table.contains(f.function)) {
            llvm::outs() << "WTF???" << "\n";
        }
        assert(table.find(f.function) == table.end());
        table.insert({f.function, f});
    }

    return table;
}

void crossover::write_func_requests(const std::string &outFile, const std::vector<std::string> &funcs) {
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

void crossover::write_replacements_requests(const std::string &out_file, const ReplacementsRequest &funcs) {
    nlohmann::json json = funcs;
    std::ofstream out(out_file);
    if (out.is_open()) {
        out << json.dump(4);
        out.close();
    } else {
        throw std::runtime_error("unable to open file for writing");
    }
}
