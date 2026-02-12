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
 *
 * Following the Configuration-to-Script pattern: this class only generates
 * strings. It never calls system() or QProcess. Execution is delegated
 * to the orchestration layer via ToolDriver.
 */
class YosysTask {
public:
    /**
     * Generate a standard Yosys synthesis TCL script.
     */
    static QString generateTcl(const SynthesisConfig& config);
    
    /**
     * Generate a Yosys script that outputs structured JSON stats.
     * Uses `tee -o /dev/stdout` and `stat -json` (Yosys 0.30+).
     * Falls back to text `stat` for older versions.
     */
    static QString generateJsonTcl(const SynthesisConfig& config);

    /**
     * Generate command-line arguments for batch-mode Yosys execution.
     * @param scriptPath Path to the generated TCL script.
     * @return Command string: "yosys -s <scriptPath>"
     */
    static QString generateBatchCommand(const QString& scriptPath);
    
private:
    static QString getGoalOptimizations(SynthesisGoal goal);
};

} // namespace acutesim::compute::orchestration
