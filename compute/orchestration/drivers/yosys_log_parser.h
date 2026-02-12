#pragma once

#include <QString>
#include <QStringList>
#include <QMap>

namespace acutesim::compute::orchestration {

/**
 * @brief Structured result from a Yosys synthesis run.
 *
 * Populated by parsing Yosys stdout, specifically the output of
 * `stat -liberty <lib>.lib`. Supports per-module breakdown
 * since Yosys logs can emit stats for multiple modules.
 */
struct ModuleStats {
    QString moduleName;
    int cellCount = 0;
    int wireCount = 0;
    int wireBits = 0;
    int publicWires = 0;
    int memories = 0;
    int memoryBits = 0;
    int processes = 0;
    double chipArea = 0.0;           // From "Chip area for module" line

    QMap<QString, int> cellBreakdown; // e.g. {"$_NOT_": 4, "sky130_inv": 2}
};

struct SynthesisResult {
    bool success = false;
    
    // Top-level stats (typically the last module reported)
    int cellCount = 0;
    int wireCount = 0;
    double estimatedArea = 0.0;

    // Per-module breakdown (key = module name)
    QMap<QString, ModuleStats> modules;

    // Cell type breakdown (aggregated across all modules)
    QMap<QString, int> cellBreakdown;

    QStringList warnings;
    QStringList errors;

    // Raw output for debugging
    QString rawOutput;
};

/**
 * @brief Parses Yosys stdout/stderr to extract structured synthesis results.
 *
 * Handles the standard Yosys `stat` output format:
 *
 *     === <module_name> ===
 *        Number of wires:               31
 *        Number of wire bits:            42
 *        Number of cells:                12
 *          $_NOT_                          4
 *          sky130_fd_sc_hd__inv_1          8
 *        Chip area for module '\top': 1234.560000
 *
 * Also extracts Warning/ERROR lines with their context.
 */
class YosysLogParser {
public:
    /**
     * Parse the complete stdout output from a Yosys run.
     */
    static SynthesisResult parse(const QString& output);

    /**
     * Parse an error/warning stream (stderr) separately.
     * Merges results into an existing SynthesisResult.
     */
    static void parseErrors(const QString& stderr, SynthesisResult& result);

    /**
     * Quick check: did the run succeed?
     * Yosys exits 0 on success but may still have warnings.
     * A run with "ERROR:" in output is considered failed.
     */
    static bool isSuccessful(const QString& output);

private:
    static ModuleStats parseModuleBlock(const QStringList& lines, int startLine, int& endLine);
};

} // namespace acutesim::compute::orchestration
