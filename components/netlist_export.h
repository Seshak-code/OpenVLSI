#pragma once
#include <string>
#include <unordered_map>
#include "circuit.h"

namespace NetlistExporter {

    enum class ExportFlavor {
        PureDigital,      // Verilog only, no R/C, ideal timing
        AnalogMixedSignal,// Verilog-AMS (modules + electrical primitives)
        Physical          // Full SPEF (detailed R/C trees) + Structural Verilog
    };

    struct ExportOptions {
        ExportFlavor flavor = ExportFlavor::Physical;
        bool useExplicitPower = true;   // VDD/VSS pins
        std::string topModuleName = "top";
        
        // Canonical Power Net Naming (Critical for Downstream Tools)
        // Maps internal PowerDomain enums/IDs to standard names like "VDD", "VSS", "VDD_IO"
        std::unordered_map<std::string, std::string> powerNetNames = {
            {"VDD", "VDD"}, {"VSS", "VSS"}, {"GND", "VSS"} 
        };
    };

    // Structural Verilog (IEEE 1364-2005)
    std::string toVerilog(const TensorNetlist& netlist, const ExportOptions& options = {});

    // Standard Parasitic Exchange Format (IEEE 1481-1999)
    std::string toSPEF(const TensorNetlist& netlist, const ExportOptions& options = {});

} // namespace NetlistExporter
