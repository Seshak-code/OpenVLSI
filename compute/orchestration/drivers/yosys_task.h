#pragma once

#include <QString>
#include <QStringList>
#include <QMap>

namespace acutesim::compute::orchestration {

enum class SynthesisGoal {
    AREA,
    SPEED,
    POWER,
    BALANCED
};

struct SynthesisConfig {
    QString topModule;
    QStringList sourceFiles;
    QString targetLibrary = "sky130_fd_sc_hd"; // Default to Skywater 130
    SynthesisGoal goal = SynthesisGoal::BALANCED;
    bool flatten = false;
};

/**
 * @brief Generates Yosys TCL scripts based on semantic intent.
 */
class YosysTask {
public:
    static QString generateTcl(const SynthesisConfig& config);
    
private:
    static QString getGoalOptimizations(SynthesisGoal goal);
};

} // namespace acutesim::compute::orchestration
