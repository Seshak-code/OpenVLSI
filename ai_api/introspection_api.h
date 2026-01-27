#pragma once

#include "../components/circuit.h"
#include "../components/component_library.h"
#include "../physics/design_rules.h"
#include <string>
#include <sstream>
#include <vector>
#include <map>
#include <functional>

/**
 * introspection_api.h
 * 
 * Circuit Introspection API for AI and external tool integration.
 * 
 * Purpose:
 * - Provide complete circuit state as JSON for AI copilot context
 * - Expose DRC/LVS rules in a PDK-adaptable format
 * - Enable AI-driven design with awareness of physical constraints
 * 
 * Architecture:
 * - PDK-agnostic base rules with override capability
 * - Rule registry pattern for easy PDK customization
 * - JSON output for cross-platform compatibility
 */

namespace Introspection {

// ============================================================================
// DRC/LVS RULE DEFINITION (PDK-ADAPTABLE)
// ============================================================================

/**
 * RuleCategory - Categories for design rules
 */
enum class RuleCategory {
    ELECTRICAL,    // ERC: floating nodes, shorts, etc.
    GEOMETRIC,     // DRC: spacing, width, enclosure
    CONNECTIVITY,  // LVS: netlist vs layout matching
    THERMAL,       // Power dissipation limits
    SIGNAL_INTEGRITY  // Crosstalk, EMC
};

/**
 * RuleSeverity - How critical is this rule?
 */
enum class RuleSeverity {
    INFO,      // Advisory
    WARNING,   // Should fix
    ERROR,     // Must fix
    FATAL      // Cannot proceed
};

/**
 * DesignRule - A single DRC/LVS rule definition
 * 
 * PDK-Adaptable: Each rule has a base definition that can be
 * overridden by PDK-specific values.
 */
struct DesignRule {
    std::string id;           // Unique identifier: "DRC.MIN_SPACING.METAL1"
    std::string name;         // Human-readable: "Metal 1 Minimum Spacing"
    std::string description;  // Full description for AI context
    
    RuleCategory category = RuleCategory::ELECTRICAL;
    RuleSeverity defaultSeverity = RuleSeverity::ERROR;
    
    // PDK-adaptable parameters
    double value = 0.0;       // Numeric threshold (µm, Ω, W, etc.)
    std::string unit;         // "um", "ohm", "W", "A"
    
    // PDK override info
    std::string pdkSource;    // Empty = generic, else PDK name
    bool isOverridden = false;
    
    // Check function (returns true if rule passes)
    std::function<bool(const TensorBlock&)> checkFn;
    
    // AI context: what should the AI do if this rule fails?
    std::string aiRemediationHint;
};

/**
 * RuleRegistry - Central registry for all DRC/LVS rules
 * 
 * Pattern: Start with generic rules, overlay PDK-specific overrides.
 */
class RuleRegistry {
public:
    static RuleRegistry& instance() {
        static RuleRegistry reg;
        return reg;
    }
    
    // Register a rule
    void registerRule(const DesignRule& rule) {
        rules_[rule.id] = rule;
    }
    
    // Override a rule with PDK-specific values
    void overrideFromPDK(const std::string& ruleId, 
                         double newValue, 
                         const std::string& pdkName,
                         RuleSeverity newSeverity = RuleSeverity::ERROR) {
        if (rules_.count(ruleId)) {
            rules_[ruleId].value = newValue;
            rules_[ruleId].pdkSource = pdkName;
            rules_[ruleId].isOverridden = true;
            rules_[ruleId].defaultSeverity = newSeverity;
        }
    }
    
    // Get all rules
    const std::map<std::string, DesignRule>& getAllRules() const {
        return rules_;
    }
    
    // Get rules by category
    std::vector<DesignRule> getRulesByCategory(RuleCategory cat) const {
        std::vector<DesignRule> result;
        for (const auto& [id, rule] : rules_) {
            if (rule.category == cat) result.push_back(rule);
        }
        return result;
    }
    
    // Serialize all rules to JSON (for AI API)
    std::string toJSON() const;
    
private:
    RuleRegistry() { initializeGenericRules(); }
    void initializeGenericRules();
    
    std::map<std::string, DesignRule> rules_;
};

// ============================================================================
// CIRCUIT STATE SNAPSHOT
// ============================================================================

/**
 * CircuitState - Complete snapshot of circuit for AI context
 */
struct CircuitState {
    // Identity
    std::string name;
    uint64_t topologyHash = 0;
    
    // Component counts
    int numNodes = 0;
    int numResistors = 0;
    int numCapacitors = 0;
    int numInductors = 0;
    int numDiodes = 0;
    int numBJTs = 0;
    int numMOSFETs = 0;
    int numVoltageSources = 0;
    int numCurrentSources = 0;
    
    // Simulation state
    bool hasSimulationResults = false;
    std::vector<double> nodeVoltages;
    bool converged = false;
    int iterations = 0;
    double residual = 0.0;
    
    // DRC/LVS status (Raw State)
    int drcErrors = 0;
    int drcWarnings = 0;
    std::vector<std::string> drcViolations;
    
    // Serialize to JSON
    std::string toJSON() const;
};

// ============================================================================
// MAIN INTROSPECTION API
// ============================================================================

/**
 * getCircuitState() - Main entry point for AI API
 * 
 * Returns complete circuit state as JSON including:
 * - Topology summary
 * - Component counts
 * - Simulation results (if available)
 * - DRC/LVS violations (Validated in Raw State)
 */
inline CircuitState getCircuitState(const TensorNetlist& netlist,
                                    const std::vector<double>& voltages = {}) {
    CircuitState state;
    
    const auto& block = netlist.globalBlock;
    
    // Identity
    state.name = block.name;
    state.topologyHash = block.topologyHash;
    
    // Component counts
    state.numNodes = netlist.numGlobalNodes;
    state.numResistors = block.resistors.size();
    state.numCapacitors = block.capacitors.size();
    state.numInductors = block.inductors.size();
    state.numDiodes = block.diodes.size();
    state.numBJTs = block.bjts.size();
    state.numMOSFETs = block.mosfets.size();
    state.numVoltageSources = block.voltageSources.size();
    state.numCurrentSources = block.currentSources.size();
    
    // Simulation state
    if (!voltages.empty()) {
        state.hasSimulationResults = true;
        state.nodeVoltages = voltages;
        state.converged = true;  // Assume converged if we have voltages
    }
    
    // Run DRC validation (Raw State)
    DesignRules::ViolationReport report = DesignRules::validate(block);
    
    state.drcErrors = 0;
    state.drcWarnings = 0;
    
    for (const auto& e : report.entries) {
        state.drcViolations.push_back(e.message);
        if (e.getSeverity() == DesignRules::ViolationEntry::Severity::ERR) {
            state.drcErrors++;
        } else if (e.getSeverity() == DesignRules::ViolationEntry::Severity::WARN) {
            state.drcWarnings++;
        }
    }
    
    return state;
}

/**
 * getCircuitStateJSON() - Convenience wrapper returning JSON string
 */
inline std::string getCircuitStateJSON(const TensorNetlist& netlist,
                                       const std::vector<double>& voltages = {}) {
    return getCircuitState(netlist, voltages).toJSON();
}

/**
 * getRulesJSON() - Get all DRC/LVS rules for AI context
 */
inline std::string getRulesJSON() {
    return RuleRegistry::instance().toJSON();
}

// ============================================================================
// JSON SERIALIZATION IMPLEMENTATIONS
// ============================================================================

inline std::string CircuitState::toJSON() const {
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"name\": \"" << name << "\",\n";
    ss << "  \"topologyHash\": " << topologyHash << ",\n";
    ss << "  \"precision\": \"emulated_f64\",\n";
    
    // Component counts
    ss << "  \"components\": {\n";
    ss << "    \"nodes\": " << numNodes << ",\n";
    ss << "    \"resistors\": " << numResistors << ",\n";
    ss << "    \"capacitors\": " << numCapacitors << ",\n";
    ss << "    \"inductors\": " << numInductors << ",\n";
    ss << "    \"diodes\": " << numDiodes << ",\n";
    ss << "    \"bjts\": " << numBJTs << ",\n";
    ss << "    \"mosfets\": " << numMOSFETs << ",\n";
    ss << "    \"voltageSources\": " << numVoltageSources << ",\n";
    ss << "    \"currentSources\": " << numCurrentSources << "\n";
    ss << "  },\n";
    
    // Simulation results
    ss << "  \"simulation\": {\n";
    ss << "    \"hasResults\": " << (hasSimulationResults ? "true" : "false") << ",\n";
    ss << "    \"converged\": " << (converged ? "true" : "false") << ",\n";
    ss << "    \"iterations\": " << iterations << ",\n";
    ss << "    \"residual\": " << residual << ",\n";
    ss << "    \"nodeVoltages\": [";
    for (size_t i = 0; i < nodeVoltages.size(); ++i) {
        ss << nodeVoltages[i];
        if (i < nodeVoltages.size() - 1) ss << ", ";
    }
    ss << "]\n";
    ss << "  },\n";
    
    // DRC status
    ss << "  \"drc\": {\n";
    ss << "    \"errors\": " << drcErrors << ",\n";
    ss << "    \"warnings\": " << drcWarnings << ",\n";
    ss << "    \"violations\": [";
    for (size_t i = 0; i < drcViolations.size(); ++i) {
        ss << "\"" << drcViolations[i] << "\"";
        if (i < drcViolations.size() - 1) ss << ", ";
    }
    ss << "]\n";
    ss << "  }\n";
    
    ss << "}";
    return ss.str();
}

inline std::string RuleRegistry::toJSON() const {
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"rules\": [\n";
    
    size_t i = 0;
    for (const auto& [id, rule] : rules_) {
        ss << "    {\n";
        ss << "      \"id\": \"" << rule.id << "\",\n";
        ss << "      \"name\": \"" << rule.name << "\",\n";
        ss << "      \"description\": \"" << rule.description << "\",\n";
        ss << "      \"category\": \"";
        switch (rule.category) {
            case RuleCategory::ELECTRICAL: ss << "electrical"; break;
            case RuleCategory::GEOMETRIC: ss << "geometric"; break;
            case RuleCategory::CONNECTIVITY: ss << "connectivity"; break;
            case RuleCategory::THERMAL: ss << "thermal"; break;
            case RuleCategory::SIGNAL_INTEGRITY: ss << "signal_integrity"; break;
        }
        ss << "\",\n";
        ss << "      \"severity\": \"";
        switch (rule.defaultSeverity) {
            case RuleSeverity::INFO: ss << "info"; break;
            case RuleSeverity::WARNING: ss << "warning"; break;
            case RuleSeverity::ERROR: ss << "error"; break;
            case RuleSeverity::FATAL: ss << "fatal"; break;
        }
        ss << "\",\n";
        ss << "      \"value\": " << rule.value << ",\n";
        ss << "      \"unit\": \"" << rule.unit << "\",\n";
        ss << "      \"pdkSource\": \"" << (rule.pdkSource.empty() ? "generic" : rule.pdkSource) << "\",\n";
        ss << "      \"isOverridden\": " << (rule.isOverridden ? "true" : "false") << ",\n";
        ss << "      \"aiHint\": \"" << rule.aiRemediationHint << "\"\n";
        ss << "    }";
        if (++i < rules_.size()) ss << ",";
        ss << "\n";
    }
    
    ss << "  ]\n";
    ss << "}";
    return ss.str();
}

inline void RuleRegistry::initializeGenericRules() {
    // ========================================================================
    // ELECTRICAL RULES (ERC)
    // ========================================================================
    
    registerRule({
        "ERC.FLOATING_GATE",
        "Floating Gate",
        "MOSFET gate has no DC path to ground. Gate capacitance can charge to arbitrary voltage.",
        RuleCategory::ELECTRICAL,
        RuleSeverity::ERROR,
        0.0, "",
        "", false,
        nullptr,
        "Add a high-value resistor (>1MΩ) from gate to a defined potential."
    });
    
    registerRule({
        "ERC.FLOATING_NODE",
        "Floating Node",
        "Circuit node has no DC path to ground reference.",
        RuleCategory::ELECTRICAL,
        RuleSeverity::ERROR,
        0.0, "",
        "", false,
        nullptr,
        "Ensure all nodes have at least one resistive path to ground."
    });
    
    registerRule({
        "ERC.VDD_GND_SHORT",
        "Power Rail Short",
        "Very low resistance path between VDD and GND.",
        RuleCategory::ELECTRICAL,
        RuleSeverity::FATAL,
        1.0, "ohm",
        "", false,
        nullptr,
        "Remove the low-resistance path or check for unintended connections."
    });
    
    registerRule({
        "ERC.MULTIPLE_DRIVERS",
        "Multiple Drivers",
        "Net has multiple active drivers which may conflict.",
        RuleCategory::ELECTRICAL,
        RuleSeverity::WARNING,
        0.0, "",
        "", false,
        nullptr,
        "Use tri-state buffers or remove conflicting drivers."
    });
    
    // ========================================================================
    // GEOMETRIC RULES (DRC) - PDK-overridable
    // ========================================================================
    
    registerRule({
        "DRC.MIN_WIDTH.METAL1",
        "Metal 1 Minimum Width",
        "Minimum allowed width for Metal 1 traces.",
        RuleCategory::GEOMETRIC,
        RuleSeverity::ERROR,
        0.14, "um",  // Generic 180nm-ish value
        "", false,
        nullptr,
        "Increase trace width to meet minimum requirement."
    });
    
    registerRule({
        "DRC.MIN_SPACING.METAL1",
        "Metal 1 Minimum Spacing",
        "Minimum spacing between Metal 1 traces.",
        RuleCategory::GEOMETRIC,
        RuleSeverity::ERROR,
        0.14, "um",
        "", false,
        nullptr,
        "Increase spacing between traces."
    });
    
    registerRule({
        "DRC.MIN_WIDTH.POLY",
        "Polysilicon Minimum Width",
        "Minimum gate length for polysilicon.",
        RuleCategory::GEOMETRIC,
        RuleSeverity::ERROR,
        0.18, "um",
        "", false,
        nullptr,
        "Increase polysilicon width to meet PDK minimum."
    });
    
    registerRule({
        "DRC.VIA_ENCLOSURE",
        "Via Enclosure",
        "Metal must extend beyond via edge by minimum enclosure.",
        RuleCategory::GEOMETRIC,
        RuleSeverity::ERROR,
        0.05, "um",
        "", false,
        nullptr,
        "Extend metal overlap around via."
    });
    
    // ========================================================================
    // CONNECTIVITY RULES (LVS)
    // ========================================================================
    
    registerRule({
        "LVS.DEVICE_MISMATCH",
        "Device Count Mismatch",
        "Number of devices in layout does not match schematic.",
        RuleCategory::CONNECTIVITY,
        RuleSeverity::ERROR,
        0.0, "",
        "", false,
        nullptr,
        "Verify all schematic devices are placed in layout."
    });
    
    registerRule({
        "LVS.NET_MISMATCH",
        "Net Connectivity Mismatch",
        "Layout connectivity differs from schematic.",
        RuleCategory::CONNECTIVITY,
        RuleSeverity::ERROR,
        0.0, "",
        "", false,
        nullptr,
        "Trace the mismatched net and correct routing."
    });
    
    registerRule({
        "LVS.OPEN_NET",
        "Open Net",
        "Net in schematic has no corresponding layout connection.",
        RuleCategory::CONNECTIVITY,
        RuleSeverity::ERROR,
        0.0, "",
        "", false,
        nullptr,
        "Complete the routing for this net."
    });
    
    // ========================================================================
    // THERMAL RULES
    // ========================================================================
    
    registerRule({
        "THERMAL.MAX_POWER",
        "Maximum Device Power",
        "Device power dissipation exceeds safe operating limit.",
        RuleCategory::THERMAL,
        RuleSeverity::WARNING,
        0.5, "W",  // Generic 500mW limit
        "", false,
        nullptr,
        "Add heat sinking or reduce operating current."
    });
    
    registerRule({
        "THERMAL.CURRENT_DENSITY",
        "Electromigration Limit",
        "Current density exceeds electromigration threshold.",
        RuleCategory::THERMAL,
        RuleSeverity::ERROR,
        1e6, "A/cm2",
        "", false,
        nullptr,
        "Widen trace or add parallel paths to reduce current density."
    });
    
    // ========================================================================
    // SIGNAL INTEGRITY RULES
    // ========================================================================
    
    registerRule({
        "SI.CROSSTALK",
        "Crosstalk Risk",
        "Parallel high-impedance traces may cause signal coupling.",
        RuleCategory::SIGNAL_INTEGRITY,
        RuleSeverity::WARNING,
        10.0, "ratio",  // length/spacing ratio threshold
        "", false,
        nullptr,
        "Increase spacing or add shielding between critical nets."
    });
    
    registerRule({
        "SI.EMC_LOOP_AREA",
        "EMC Loop Area",
        "Large current loop area may cause EMI issues.",
        RuleCategory::SIGNAL_INTEGRITY,
        RuleSeverity::WARNING,
        10000.0, "um2",
        "", false,
        nullptr,
        "Minimize loop area by routing return path adjacent to signal."
    });
}

// ============================================================================
// PDK LOADING INTERFACE (Future Extension Point)
// ============================================================================

/**
 * loadPDKRules() - Load PDK-specific rule overrides
 * 
 * Future: Parse PDK rule files (e.g., Calibre DRC deck, OpenLane config)
 * and override the generic rules with foundry-specific values.
 */
inline void loadPDKRules(const std::string& pdkName) {
    auto& registry = RuleRegistry::instance();
    
    // Example PDK overrides
    if (pdkName == "sky130") {
        registry.overrideFromPDK("DRC.MIN_WIDTH.METAL1", 0.14, "sky130");
        registry.overrideFromPDK("DRC.MIN_SPACING.METAL1", 0.14, "sky130");
        registry.overrideFromPDK("DRC.MIN_WIDTH.POLY", 0.15, "sky130");
        registry.overrideFromPDK("DRC.VIA_ENCLOSURE", 0.06, "sky130");
    } else if (pdkName == "gf180mcu") {
        registry.overrideFromPDK("DRC.MIN_WIDTH.METAL1", 0.23, "gf180mcu");
        registry.overrideFromPDK("DRC.MIN_SPACING.METAL1", 0.23, "gf180mcu");
        registry.overrideFromPDK("DRC.MIN_WIDTH.POLY", 0.18, "gf180mcu");
    }
    // Add more PDKs as needed
}

} // namespace Introspection
