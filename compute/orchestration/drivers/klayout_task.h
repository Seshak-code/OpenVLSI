#pragma once

#include <string>
#include <vector>

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
    std::string layoutFile;                 // Input GDS/OASIS file
    std::string ruleFile;                   // DRC rule deck (KLayout Ruby or DRC)
    std::string techFile;                   // Technology file
    std::string outputReport;               // Output report file
    DRCLevel minLevel = DRCLevel::ERROR;    // Minimum level to report
    bool verbose = false;                   // Verbose output
    std::vector<std::string> excludeRules;  // Rules to skip
    std::vector<std::string> onlyRules;     // Run only these rules
};

/**
 * Configuration for Layout vs Schematic check.
 */
struct LVSConfig {
    std::string layoutFile;             // Input GDS/OASIS file
    std::string schematicNetlist;       // Reference netlist (SPICE/Verilog)
    std::string ruleFile;               // LVS rule deck
    std::string techFile;               // Technology file
    std::string outputReport;           // Output report file
    LVSMode mode = LVSMode::HIERARCHICAL;
    bool extractParasitics = false;     // Extract RC parasitics
    std::string topCell;                // Top cell name (if different)
};

/**
 * Configuration for parasitic extraction.
 */
struct PEXConfig {
    std::string layoutFile;             // Input GDS/OASIS file
    std::string techFile;               // Technology/RC extraction file
    std::string outputSpef;             // Output SPEF file
    std::string outputSpice;            // Output SPICE netlist
    bool couplingCaps = true;           // Include coupling capacitances
    double minResistance = 0.0;         // Min resistance to extract (ohms)
    double minCapacitance = 0.0;        // Min capacitance to extract (fF)
};

/**
 * Configuration for GDSII export via KLayout.
 */
struct GDSExportConfig {
    std::string inputFile;          // Input OASIS/GDS/DEF file
    std::string techFile;           // Technology file (layer mapping)
    std::string outputGDS;          // Output .gds file path
    std::string topCell;            // Top cell to export (empty = all)
    bool compressOutput = true;     // Write .gds.gz if true
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
    static std::string generateDRCScript(const DRCConfig& config);
    static std::string generateLVSScript(const LVSConfig& config);
    static std::string generatePEXScript(const PEXConfig& config);
    static std::string generateGDSExportScript(const GDSExportConfig& config);
    static std::string generateBatchCommand(const std::string& scriptPath);

private:
    static std::string getDRCLevelFilter(DRCLevel level);
    static std::string getLVSModeOptions(LVSMode mode);
};

} // namespace acutesim::compute::orchestration
