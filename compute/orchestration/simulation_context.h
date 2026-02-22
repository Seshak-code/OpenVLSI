#pragma once
#include <QString>
#include <QHash>
#include <vector>
#include <string>

namespace acutesim::orchestration {

enum class SimulationType {
    None,
    Analog,         // Tensor MNA (Schematic / Verilog-A)
    Digital,        // HDL (Verilog/SystemVerilog)
    MixedSignal,    // Cosimulation (Analog + Digital)
    Script          // Python / TCL
};

struct FileRef {
    QString path;
    bool isTop = false;       // Is this the entry point?
    bool isSchematic = false; // .sch vs code
};

struct SimulationContext {
    SimulationType type = SimulationType::None;
    QString contextId; // Unique ID (e.g. project name or main file name)
    
    std::vector<FileRef> sources;
    QString entryPoint; // Top module name or testbench file
    
// For overriding settings
    QString solverMode; 
    QString precision;
    
    // ─── Design Variables (Global Parameters) ─────────────────────────────
    // Core of the "Orchestration Layer": Variables are resolved HERE,
    // before the NetlistCompiler sees them. The solver stays numeric-only.

    QHash<QString, double> variables;         // Resolved numeric values (e.g. "Rload" -> 1000.0)
    QHash<QString, QString> variableExprs;    // Expressions (e.g. "Rload" -> "2*Rbias")
    QHash<QString, bool> variableEnabled;     // Per-variable toggle

    // ─── Resolution Logic ────────────────────────────────────────────────
    // Topological sort to resolve dependencies (A=B, B=10 -> Order: B, then A)
    // Returns false if circular dependency detected or expression invalid.
    bool resolveVariables(QString* errorMsg = nullptr);
    
    // Evaluate a single expression in the context of current variables
    // Returns NaN on failure.
    double evaluate(const QString& expr) const;

    bool isValid() const { return type != SimulationType::None && !sources.empty(); }
};

} // namespace acutesim::orchestration
