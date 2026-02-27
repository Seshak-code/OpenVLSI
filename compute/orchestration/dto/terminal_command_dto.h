#pragma once
// ============================================================================
// terminal_command_dto.h — Terminal/Console Command DTOs
// ============================================================================
// ARCHITECTURAL RULE: This header must NEVER include Qt, Eigen, Physics, or
// any Solver types. Only standard library types are allowed.
// Commands are parsed by the driver, never by the GUI.
// ============================================================================

#include <string>
#include <vector>

namespace acutesim {
namespace compute {
namespace orchestration {

struct TerminalCommandDTO {
    std::string rawCommand;     // e.g. "run dc", "sweep R1 1k 10k 10"
    std::string sessionToken;   // For session tracking
};

struct TerminalResponseDTO {
    bool        success = false;
    std::string output;         // Human-readable result or info
    std::string errorMessage;   // Non-empty on failure
};

} // namespace orchestration
} // namespace compute
} // namespace acutesim
