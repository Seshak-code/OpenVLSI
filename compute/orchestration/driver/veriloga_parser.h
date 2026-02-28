#pragma once
// ============================================================================
// veriloga_parser.h — Restricted Verilog-A Parser for Machine-Generated Source
// ============================================================================
// Parses the machine-generated subset produced by VerilogAGenerator and
// reconstructs a TensorNetlist. NOT a full Verilog-A parser — only handles
// the exact patterns produced by the generator.
//
// Architectural note: Lives in compute/orchestration/driver/ because it
// reconstructs TensorNetlist (from components/, which compute depends on).
// ============================================================================

#include <string>
#include "acutesim_engine/netlist/circuit.h"

namespace acutesim {
namespace compute {
namespace orchestration {

struct VerilogAParseResult {
    TensorNetlist netlist;
    bool          success      = false;
    std::string   errorDetail;
    int           errorLine    = -1;
};

// Parse Verilog-A source produced by VerilogAGenerator::generate().
// Returns a populated TensorNetlist on success, errorDetail on failure.
// Falls back gracefully: on any unrecognized pattern the item is skipped
// and a warning is appended to errorDetail but success can still be true
// if core components were parsed.
VerilogAParseResult parseVerilogA(const std::string& source);

} // namespace orchestration
} // namespace compute
} // namespace acutesim
