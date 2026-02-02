#pragma once
#include <QString>
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
    
    bool isValid() const { return type != SimulationType::None && !sources.empty(); }
};

} // namespace acutesim::orchestration
