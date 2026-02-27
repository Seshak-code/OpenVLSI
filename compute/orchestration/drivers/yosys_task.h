#pragma once

#include <string>
#include <vector>

namespace acutesim::compute::orchestration {

enum class SynthesisGoal {
    AREA,
    SPEED,
    POWER,
    BALANCED
};

struct SynthesisConfig {
    std::string topModule;
    std::vector<std::string> sourceFiles;
    std::string targetLibrary = "sky130_fd_sc_hd"; // Default to Skywater 130
    SynthesisGoal goal = SynthesisGoal::BALANCED;
    bool flatten = false;
};

/**
 * @brief Generates Yosys TCL scripts based on semantic intent.
 *
 * Following the Configuration-to-Script pattern: this class only generates
 * strings. It never calls system() or QProcess. Execution is delegated
 * to the orchestration layer via ToolDriver.
 */
class YosysTask {
public:
    static std::string generateTcl(const SynthesisConfig& config);
    static std::string generateJsonTcl(const SynthesisConfig& config);
    static std::string generateBatchCommand(const std::string& scriptPath);

private:
    static std::string getGoalOptimizations(SynthesisGoal goal);
};

} // namespace acutesim::compute::orchestration
