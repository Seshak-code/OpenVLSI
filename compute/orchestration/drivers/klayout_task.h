#pragma once

#include <QString>
#include <QStringList>

namespace acutesim::compute::orchestration {

/**
 * DRC rule severity level.
 */
enum class DRCLevel {
    ERROR,      // Must fix before tapeout
    WARNING,    // Should fix
    INFO        // Informational only
};

/**
 * LVS comparison mode.
 */
enum class LVSMode {
    FLAT,           // Flat comparison
    HIERARCHICAL,   // Hierarchical comparison
    DEVICE_LEVEL    // Device-level extraction
};

/**
 * Configuration for Design Rule Check.
 */
struct DRCConfig {
    QString layoutFile;             // Input GDS/OASIS file
    QString ruleFile;               // DRC rule deck (KLayout Ruby or DRC)
    QString techFile;               // Technology file
    QString outputReport;           // Output report file
    DRCLevel minLevel = DRCLevel::ERROR;  // Minimum level to report
    bool verbose = false;           // Verbose output
    QStringList excludeRules;       // Rules to skip
    QStringList onlyRules;          // Run only these rules
};

/**
 * Configuration for Layout vs Schematic check.
 */
struct LVSConfig {
    QString layoutFile;             // Input GDS/OASIS file
    QString schematicNetlist;       // Reference netlist (SPICE/Verilog)
    QString ruleFile;               // LVS rule deck
    QString techFile;               // Technology file
    QString outputReport;           // Output report file
    LVSMode mode = LVSMode::HIERARCHICAL;
    bool extractParasitics = false; // Extract RC parasitics
    QString topCell;                // Top cell name (if different)
};

/**
 * Configuration for parasitic extraction.
 */
struct PEXConfig {
    QString layoutFile;             // Input GDS/OASIS file
    QString techFile;               // Technology/RC extraction file
    QString outputSpef;             // Output SPEF file
    QString outputSpice;            // Output SPICE netlist
    bool couplingCaps = true;       // Include coupling capacitances
    double minResistance = 0.0;     // Min resistance to extract (ohms)
    double minCapacitance = 0.0;    // Min capacitance to extract (fF)
};

/**
 * @brief Generates KLayout scripts for DRC, LVS, and PEX.
 *
 * KLayout handles:
 * - DRC: Design Rule Checking against foundry rules
 * - LVS: Layout vs Schematic comparison
 * - PEX: Parasitic extraction for timing/signal integrity
 *
 * Following the Configuration-to-Script pattern, this class only generates
 * Ruby/DRC scripts. Execution is handled by the orchestration layer.
 */
class KLayoutTask {
public:
    /**
     * Generate DRC script.
     * Checks layout against design rules.
     */
    static QString generateDRCScript(const DRCConfig& config);

    /**
     * Generate LVS script.
     * Compares extracted layout netlist against schematic.
     */
    static QString generateLVSScript(const LVSConfig& config);

    /**
     * Generate parasitic extraction script.
     */
    static QString generatePEXScript(const PEXConfig& config);

    /**
     * Generate KLayout batch command line.
     * Returns the command to run KLayout in batch mode with the script.
     */
    static QString generateBatchCommand(const QString& scriptPath);

private:
    static QString getDRCLevelFilter(DRCLevel level);
    static QString getLVSModeOptions(LVSMode mode);
};

} // namespace acutesim::compute::orchestration
