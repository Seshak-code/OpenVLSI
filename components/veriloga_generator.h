#pragma once
// ============================================================================
// veriloga_generator.h — Verilog-A Contribution-Statement Generator
// ============================================================================
// Converts a TensorNetlist into a single-module Verilog-A source file using
// `analog begin ... end` contribution statements for ALL 9 component types.
//
// Architectural note: Lives in components/ (same layer as netlist_export.h)
// because it operates solely on TensorNetlist. Pure C++, no Qt.
//
// Generated output is machine-readable by VerilogAParser (in
// compute/orchestration/driver/) and round-trips losslessly for all supported
// component types. FP determinism is preserved via %.17g formatting.
// ============================================================================

#include <string>
#include "circuit.h"
#include "../compute/orchestration/dto/schematic_dto.h"

namespace VerilogAGenerator {

struct GeneratorOptions {
    std::string topModuleName = "top";
    bool        includeProbes = true;  // Emit // .probe annotations
};

// Generate a Verilog-A module from a TensorNetlist.
// Returns a UTF-8 string containing the complete .va source.
// Handles: Resistor, Capacitor, Inductor, VoltageSource, CurrentSource,
//          Diode, BJT (NPN/PNP Ebers-Moll), MOSFET (Shichman-Hodges), Probe.
std::string generate(const TensorNetlist& netlist,
                     const GeneratorOptions& opts = {});

// New DTO-based entry point (GUI path)
std::string generate(const acutesim::compute::orchestration::HierarchicalSchematicDTO& hierDto,
                     const GeneratorOptions& opts = {});

} // namespace VerilogAGenerator
