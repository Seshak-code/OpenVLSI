#pragma once
#include <string>
#include <unordered_map>
#include <vector>

namespace acutesim::orchestration {

enum class SimulationType {
    None,
    Analog,         // Tensor MNA (Schematic / Verilog-A)
    Digital,        // HDL (Verilog/SystemVerilog)
    MixedSignal,    // Cosimulation (Analog + Digital)
    Script          // Python / TCL
};

struct FileRef {
    std::string path;
    bool isTop = false;       // Is this the entry point?
    bool isSchematic = false; // .sch vs code
};

struct SimulationContext {
    SimulationType type = SimulationType::None;
    std::string contextId; // Unique ID (e.g. project name or main file name)

    std::vector<FileRef> sources;
    std::string entryPoint; // Top module name or testbench file

    // For overriding settings
    std::string solverMode;
    std::string precision;

    // ─── Design Variables (Global Parameters) ─────────────────────────────
    // Core of the "Orchestration Layer": Variables are resolved HERE,
    // before the solver sees them. The solver stays numeric-only.

    std::unordered_map<std::string, double> variables;        // Resolved numeric values
    std::unordered_map<std::string, std::string> variableExprs;  // Expressions
    std::unordered_map<std::string, bool> variableEnabled;    // Per-variable toggle

    // ─── Resolution Logic ────────────────────────────────────────────────
    // Topological sort to resolve dependencies (A=B, B=10 -> Order: B, then A)
    // Returns false if circular dependency detected or expression invalid.
    bool resolveVariables(std::string* errorMsg = nullptr);

    // Evaluate a single expression in the context of current variables
    // Returns NaN on failure.
    double evaluate(const std::string& expr) const;

    bool isValid() const { return type != SimulationType::None && !sources.empty(); }
};

} // namespace acutesim::orchestration
