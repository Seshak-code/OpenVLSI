#pragma once
// ============================================================================
// terminal_parser.h — Connectivity Layer Terminal Command Parser
// ============================================================================
// PURE C++ / STL. No Qt. No GUI. No filesystem paths.
// Parses raw text commands into typed ParsedTerminalCommand structures.
//
// Lives in connectivity/ so CLI, AI terminal, remote shell, and web console
// all share the same parser without duplicating logic.
// ============================================================================

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace acutesim::connectivity::terminal {

// ── Command classes ────────────────────────────────────────────────────────────
enum class TerminalCommandClass : uint8_t {
    Simulation,   // run dc | run tran | run ac
    Netlist,      // netlist spice | netlist verilog
    Measure,      // measure gain V(out) V(in)
    Plot,         // plot V(out) I(R1)
    Control,      // help | clear | cancel <job_id>
    Unknown
};

// ── Parsed result ─────────────────────────────────────────────────────────────
struct ParsedTerminalCommand {
    TerminalCommandClass cls       = TerminalCommandClass::Unknown;
    std::string          verb;      // "run", "plot", "measure", "netlist"
    std::vector<std::string> args;  // positional arguments
    std::unordered_map<std::string, std::string> flags; // --key=value flags
    bool                 valid     = false;
    std::string          errorMsg;
};

// ── Parser ────────────────────────────────────────────────────────────────────
ParsedTerminalCommand parseTerminalCommand(const std::string& raw);

// ── Tokenizer utility (exposed for testing) ───────────────────────────────────
std::vector<std::string> tokenize(const std::string& input);

} // namespace acutesim::connectivity::terminal
