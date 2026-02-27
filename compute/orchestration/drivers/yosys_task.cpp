#include "yosys_task.h"
#include <sstream>

namespace acutesim::compute::orchestration {

std::string YosysTask::generateTcl(const SynthesisConfig& config) {
    std::ostringstream out;

    out << "# AcuteSim Auto-Generated Yosys Script\n";
    out << "# Goal: " << (config.goal == SynthesisGoal::SPEED ? "Speed" : "Area/Power") << "\n\n";

    out << "read_liberty -lib " << config.targetLibrary << ".lib\n";

    for (const auto& file : config.sourceFiles)
        out << "read_verilog " << file << "\n";

    out << "hierarchy -check -top " << config.topModule << "\n";
    out << "proc; opt; fsm; opt; memory; opt\n";
    out << "techmap; opt\n";

    std::string abcCmd = "abc -liberty " + config.targetLibrary + ".lib";
    abcCmd += getGoalOptimizations(config.goal);
    out << abcCmd << "\n";

    out << "dfflibmap -liberty " << config.targetLibrary << ".lib\n";
    out << "opt_clean -purge\n";
    out << "write_verilog -noattr syn_" << config.topModule << ".v\n";
    out << "stat -liberty " << config.targetLibrary << ".lib\n";

    return out.str();
}

std::string YosysTask::getGoalOptimizations(SynthesisGoal goal) {
    switch (goal) {
        case SynthesisGoal::AREA:
            return " -script +strash;ifraig;retime,-D,{D};dch,-f;map,-a,-M,1,{D}";
        case SynthesisGoal::SPEED:
            return " -script +strash;ifraig;retime,-D,{D};map,-p,-M,1,{D}";
        case SynthesisGoal::POWER:
            return " -script +strash;ifraig;scorr;dc2;dch,-f;map,-a,-M,1,{D}";
        default:
            return "";
    }
}

std::string YosysTask::generateJsonTcl(const SynthesisConfig& config) {
    std::ostringstream out;

    out << "# AcuteSim Auto-Generated Yosys Script (JSON Output Mode)\n";
    out << "# Yosys 0.30+ required for -json flag\n\n";

    out << "read_liberty -lib " << config.targetLibrary << ".lib\n";
    for (const auto& file : config.sourceFiles)
        out << "read_verilog " << file << "\n";
    out << "hierarchy -check -top " << config.topModule << "\n";
    out << "proc; opt; fsm; opt; memory; opt\n";
    out << "techmap; opt\n";

    std::string abcCmd = "abc -liberty " + config.targetLibrary + ".lib";
    abcCmd += getGoalOptimizations(config.goal);
    out << abcCmd << "\n";

    out << "dfflibmap -liberty " << config.targetLibrary << ".lib\n";
    out << "opt_clean -purge\n";
    out << "stat -json -liberty " << config.targetLibrary << ".lib\n";
    out << "write_verilog -noattr syn_" << config.topModule << ".v\n";

    return out.str();
}

std::string YosysTask::generateBatchCommand(const std::string& scriptPath) {
    return "yosys -s " + scriptPath;
}

} // namespace acutesim::compute::orchestration
