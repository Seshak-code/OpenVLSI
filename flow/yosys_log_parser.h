#pragma once

#include <string>
#include <vector>
#include <map>

namespace acutesim::compute::orchestration {

/**
 * @brief Structured result from a Yosys synthesis run.
 */
struct ModuleStats {
    std::string moduleName;
    int cellCount = 0;
    int wireCount = 0;
    int wireBits = 0;
    int publicWires = 0;
    int memories = 0;
    int memoryBits = 0;
    int processes = 0;
    double chipArea = 0.0;

    std::map<std::string, int> cellBreakdown;
};

struct SynthesisResult {
    bool success = false;

    int cellCount = 0;
    int wireCount = 0;
    double estimatedArea = 0.0;

    std::map<std::string, ModuleStats> modules;
    std::map<std::string, int> cellBreakdown;

    std::vector<std::string> warnings;
    std::vector<std::string> errors;

    std::string rawOutput;
};

/**
 * @brief Parses Yosys stdout/stderr to extract structured synthesis results.
 */
class YosysLogParser {
public:
    static SynthesisResult parse(const std::string& output);
    static void parseErrors(const std::string& text, SynthesisResult& result);
    static bool isSuccessful(const std::string& output);

private:
    static ModuleStats parseModuleBlock(const std::vector<std::string>& lines,
                                         int startLine, int& endLine);
};

} // namespace acutesim::compute::orchestration
