#include "yosys_task.h"
#include <QTextStream>

namespace acutesim::compute::orchestration {

QString YosysTask::generateTcl(const SynthesisConfig& config) {
    QString tcl;
    QTextStream out(&tcl);

    out << "# AcuteSim Auto-Generated Yosys Script\n";
    out << "# Goal: " << (config.goal == SynthesisGoal::SPEED ? "Speed" : "Area/Power") << "\n\n";

    // 1. Read Liberty File (Cell Library)
    out << "read_liberty -lib " << config.targetLibrary << ".lib\n";

    // 2. Read Verilog Sources
    for (const auto& file : config.sourceFiles) {
        out << "read_verilog " << file << "\n";
    }

    // 3. Elaborate Hierarchy
    out << "hierarchy -check -top " << config.topModule << "\n";

    // 4. Generic Optimization (High-Level)
    out << "proc; opt; fsm; opt; memory; opt\n";

    // 5. Tech Mapping
    out << "techmap; opt\n";
    
    // 6. ABC (Logic Synthesis & Mapping)
    QString abcCmd = "abc -liberty " + config.targetLibrary + ".lib";
    abcCmd += getGoalOptimizations(config.goal);
    out << abcCmd << "\n";

    // 7. Flip-Flop Mapping
    out << "dfflibmap -liberty " << config.targetLibrary << ".lib\n";

    // 8. Final Cleanup
    out << "opt_clean -purge\n";
    
    // 9. Output Results
    out << "write_verilog -noattr syn_" << config.topModule << ".v\n";
    out << "stat -liberty " << config.targetLibrary << ".lib\n";

    return tcl;
}

QString YosysTask::getGoalOptimizations(SynthesisGoal goal) {
    switch (goal) {
        case SynthesisGoal::AREA:
            return " -script +strash;ifraig;retime,-D,{D};dch,-f;map,-a,-M,1,{D}";
        case SynthesisGoal::SPEED:
            return " -script +strash;ifraig;retime,-D,{D};map,-p,-M,1,{D}";
        default:
            return ""; // Default ABC heuristic
    }
}

} // namespace acutesim::compute::orchestration
