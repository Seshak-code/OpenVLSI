#pragma once
// ============================================================================
// schematic_dto.h — Pure Data Transfer Object for Schematic Topologies
// ============================================================================
// ARCHITECTURAL RULE: This header must NEVER include Qt, Eigen, Physics, or
// any Solver types. Only standard library types are allowed.
// Placed in compute/orchestration/dto/ to be included by the GUI safely.
// ============================================================================

#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace acutesim {
namespace compute {
namespace orchestration {

/**
 * @brief Global Hierarchical Node Identity.
 * Guarantees uniqueness across all schematic depths.
 */
struct HierNodeId {
    std::vector<std::string> path; // e.g. ["X1", "X2"]
    std::string localNodeName;     // Local net name (e.g. "VOUT")

    bool operator<(const HierNodeId& other) const {
        if (path != other.path) return path < other.path;
        return localNodeName < other.localNodeName;
    }
    
    std::string toGlobalString() const {
        std::string full;
        for (const auto& p : path) {
            full += p + ".";
        }
        full += localNodeName;
        return full;
    }
};

// Represents a connected net/wire in the schematic.
struct SchematicNodeDTO {
    std::string name;
    int index = -1; // Optional internal index for debugging or deterministic sorting
};

// Represents a placed component (primitive or subcircuit instance).
struct SchematicInstanceDTO {
    std::string type;          // e.g., "Resistor", "Mosfet", "Inverter_Subckt"
    std::string instanceName;  // e.g., "R1", "M1", "X1"
    
    // Ordered list of net names this instance connects to, matching its symbol ports.
    std::vector<std::string> connectedNets;
    
    // Ordered pin names (if available) corresponding to connectedNets.
    std::vector<std::string> pinNames;
    
    // Key-value pairs of parameters, e.g., {"R": "1k"}, {"W": "1.5u"}
    std::map<std::string, std::string> parameters;
};

// The flat representation of the schematic canvas, decoupled from physics semantics.
struct SchematicGraphDTO {
    std::string name = "top";
    std::vector<SchematicNodeDTO> nets;
    std::vector<SchematicInstanceDTO> instances;
    
    // Ordered ports for when this graph is instantiated as a subcircuit.
    std::vector<std::string> portNames;
};

// Represents the complete hierarchical design submitted by the GUI.
struct HierarchicalSchematicDTO {
    SchematicGraphDTO topGraph;
    std::map<std::string, SchematicGraphDTO> subcircuits;
};

} // namespace orchestration
} // namespace compute
} // namespace acutesim
