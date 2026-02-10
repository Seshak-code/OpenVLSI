#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include <memory>
#include <optional>
#include <array>
#include <cstdint>
#include <cassert>
#include <random>

// Forward Declaration
struct TensorBlock;

namespace acutesim {
namespace components {

// ============================================================================
// TYPED UUID NAMESPACE
// ============================================================================

/**
 * @brief Strictly typed UUID to prevent cross-class collisions (e.g. NetUUID != PINUUID)
 * Uses a 128-bit random identifier + separate types for compile-time safety.
 */
template <typename Tag>
struct UUID {
    std::array<uint8_t, 16> bytes;

    static UUID generate() {
        UUID id;
        // Simple distinct generator for now
        // In production, use a robust PRNG or system UUID
        static std::mt19937_64 rng(std::random_device{}());
        static std::uniform_int_distribution<uint64_t> dist;
        uint64_t* ptr = reinterpret_cast<uint64_t*>(id.bytes.data());
        ptr[0] = dist(rng);
        ptr[1] = dist(rng);
        return id;
    }

    bool operator==(const UUID& other) const { return bytes == other.bytes; }
    bool operator!=(const UUID& other) const { return bytes != other.bytes; }
    bool operator<(const UUID& other) const { return bytes < other.bytes; }
    
    std::string toString() const {
        static const char hex[] = "0123456789abcdef";
        std::string s;
        s.reserve(36);
        for (int i = 0; i < 16; ++i) {
            s += hex[bytes[i] >> 4];
            s += hex[bytes[i] & 0xF];
            if (i == 3 || i == 5 || i == 7 || i == 9) s += '-';
        }
        return s;
    }

    static UUID fromString(const std::string& s) {
        UUID id;
        // Strip dashes if any
        std::string hex;
        for(char c : s) if(c != '-') hex += c;
        
        if (hex.size() == 32) {
             for(int i=0; i<16; ++i) {
                 std::string byteStr = hex.substr(i*2, 2);
                 id.bytes[i] = (uint8_t)std::stoul(byteStr, nullptr, 16);
             }
        }
        return id;
    }
};

// Tag Types
struct NetTag {};
struct PinTag {};
struct InstTag {};
struct LayerTag {};

using NetUUID = UUID<NetTag>;
using PinUUID = UUID<PinTag>;
using InstUUID = UUID<InstTag>;
using LayerUUID = UUID<LayerTag>;

// ============================================================================
// TOPOLOGICAL OBJECT MODELS
// ============================================================================

/**
 * @brief Logical Pin: Maps a Component Instance Terminal to a Net.
 * "Pin" is the logical connection point. "Terminal" is the index on the device.
 */
struct Pin {
    PinUUID id;
    InstUUID instanceId;
    int terminalIndex;      // 0=Drain, 1=Gate, etc. (Device-specific)
    NetUUID netId;          // The net this pin drives/loads
    
    // Annotations
    std::string name;       // "A", "Y", "D", "G"
};

/**
 * @brief Explicit Net: Represents a single equipotential node.
 * Tracks connected pins and physical/timing constraints.
 */
struct Net {
    NetUUID id;
    std::string name;       // "clk", "n102", "vdd"
    
    std::set<PinUUID> connectedPins;
    
    // Constraints (RTL -> GDS)
    struct Constraints {
        bool isClock = false;
        bool isPower = false;
        bool isGround = false;
        double maxCapacitance = 0.0;
        double maxTransition = 0.0;
    } constraints;
    
    // Physical Annotations (Layout Parity)
    // Extraction results annotate here, rather than replacing the net
    double rotation; // Degrees
    bool isFlippedX;
    bool isFlippedY;
};

// Phase 1.3: Placement Intent Preservation
struct PlacementConstraint {
    std::string refId; // Reference component instance name/ID
    enum Type { RELATIVE, ABSOLUTE, ALIGN } type;
    double offset; // Spacing or offset value
    std::string direction; // "East", "North", etc.
};

/**
 * @brief Component Instance: A realized device in the circuit.
 */
struct Instance {
    InstUUID id;
    std::string name;       // User-defined name (e.g., "M1", "R_Bias")
    std::string masterName; // Reference to Component Library (e.g., "nmos_3v3", "res_poly")
    
    // Parameter Blob (Key-Value)
    std::map<std::string, double> parameters;
    std::map<std::string, std::string> stringParameters;
    
    // Spatial Annotation (GUI only - NOT electrical truth)
    struct SpatialInfo {
        double x, y;
        double rotation;
        bool flipped;
    } spatial;
    
    // Phase 1.3: Placement Intent (optional storage for re-layout)
    PlacementConstraint layoutIntent;
};

// ============================================================================
// POWER DOMAIN REGISTRY (Phase 1.3 Refinement)
// ============================================================================

/**
 * @brief Power Domain Type (matches PDKPowerDomain::Type in circuit.h)
 */
enum class PowerDomainType { VDD, VSS, VDDIO, VBB };

/**
 * @brief Result of domain registration
 */
struct DomainRegisterResult {
    bool success;
    std::string message;
    int assignedNode = -1;  // Valid if success
};

/**
 * @brief Power Domain Entry: Global electrical concept.
 * Multiple power symbols can reference the same domain.
 */
struct PowerDomainEntry {
    std::string domainName;           // "CORE", "IO", "ANALOG"
    PowerDomainType type;             // VDD, VSS, VDDIO, VBB
    double nominalVoltage;            // 1.8V, 0V, 3.3V
    int globalNode = -1;              // Assigned during netlisting
    bool isImmutable = false;         // True for VSS (cannot delete/rename)
    double internalResistance = 0.05; // IR drop: V = Vnom - I * Rint
    double maxCurrent = 0.1;          // 100mA default
    
    std::vector<std::string> connectedSymbolIds;  // Visual instance IDs
};

/**
 * @brief Centralized Power Domain Registry.
 * Manages domain creation, merging, conflict detection, and ground immutability.
 */
class PowerDomainRegistry {
public:
    PowerDomainRegistry() {
        // VSS is ALWAYS present and immutable
        PowerDomainEntry vss;
        vss.domainName = "VSS";
        vss.type = PowerDomainType::VSS;
        vss.nominalVoltage = 0.0;
        vss.globalNode = 0;  // Ground is always Node 0
        vss.isImmutable = true;
        domains_["VSS"] = vss;
    }
    
    /**
     * @brief Register a domain. Returns existing or creates new.
     * MERGE RULES:
     *   - Same name + different voltage → ERROR
     *   - Same name + different type → ERROR  
     *   - Same name + same properties → MERGE (reuse node)
     */
    DomainRegisterResult registerDomain(const std::string& name, 
                                        PowerDomainType type, 
                                        double voltage,
                                        const std::string& symbolId = "") {
        if (domains_.count(name)) {
            auto& existing = domains_[name];
            
            // CONFLICT: Same name, different voltage
            constexpr double VOLTAGE_TOL = 1e-6;
            if (std::abs(existing.nominalVoltage - voltage) > VOLTAGE_TOL) {
                return {false, "Voltage conflict: '" + name + "' exists at " + 
                        std::to_string(existing.nominalVoltage) + "V, cannot register at " + 
                        std::to_string(voltage) + "V", -1};
            }
            
            // CONFLICT: Same name, different type
            if (existing.type != type) {
                return {false, "Type conflict: '" + name + "' exists as different power type", -1};
            }
            
            // MERGE: Same properties, add symbol reference
            if (!symbolId.empty()) {
                existing.connectedSymbolIds.push_back(symbolId);
            }
            return {true, "Merged with existing domain '" + name + "'", existing.globalNode};
        }
        
        // NEW DOMAIN
        PowerDomainEntry entry;
        entry.domainName = name;
        entry.type = type;
        entry.nominalVoltage = voltage;
        entry.isImmutable = (type == PowerDomainType::VSS);
        entry.globalNode = nextNodeId_++;
        if (!symbolId.empty()) {
            entry.connectedSymbolIds.push_back(symbolId);
        }
        
        domains_[name] = entry;
        return {true, "Created new domain '" + name + "'", entry.globalNode};
    }
    
    /**
     * @brief Remove a symbol from a domain. Domain persists if symbols remain.
     */
    bool unregisterSymbol(const std::string& domainName, const std::string& symbolId) {
        auto it = domains_.find(domainName);
        if (it == domains_.end()) return false;
        if (it->second.isImmutable && it->second.type == PowerDomainType::VSS) {
            // VSS domain cannot be removed
            return false;
        }
        auto& syms = it->second.connectedSymbolIds;
        syms.erase(std::remove(syms.begin(), syms.end(), symbolId), syms.end());
        return true;
    }
    
    /**
     * @brief Query domain by name
     */
    const PowerDomainEntry* getDomain(const std::string& name) const {
        auto it = domains_.find(name);
        return it != domains_.end() ? &it->second : nullptr;
    }
    
    /**
     * @brief Get all domains
     */
    const std::map<std::string, PowerDomainEntry>& getAllDomains() const {
        return domains_;
    }
    
    /**
     * @brief Check if a domain name is reserved/immutable
     */
    bool isImmutable(const std::string& name) const {
        auto it = domains_.find(name);
        return it != domains_.end() && it->second.isImmutable;
    }
    
    /**
     * @brief Assign global node IDs for netlisting
     */
    void assignNodes(int startingNode = 1) {
        nextNodeId_ = startingNode;
        for (auto& [name, entry] : domains_) {
            if (entry.type == PowerDomainType::VSS) {
                entry.globalNode = 0;  // Ground always 0
            } else {
                entry.globalNode = nextNodeId_++;
            }
        }
    }
    
private:
    std::map<std::string, PowerDomainEntry> domains_;
    int nextNodeId_ = 1;  // Node 0 is reserved for VSS
};

// ============================================================================
// VBB CONNECTIVITY VALIDATION
// ============================================================================

/**
 * @brief Validates that VBB only connects to bulk/body pins
 * @return true if connection is valid
 */
inline bool validateVBBConnection(const std::string& pinName) {
    std::string upper = pinName;
    for (auto& c : upper) c = std::toupper(c);
    return upper == "BULK" || upper == "BODY" || upper == "B" || upper == "SUB";
}

// ============================================================================
// DESIGN DB ENUMS
// ============================================================================

/**
 * @brief Mutation Result Status
 * Use this instead of bool to provide actionable feedback to Tcl/GUI.
 */
enum class MutationResult {
    Success,
    NotFound,
    NameCollision,
    InvalidName,
    NoChange,
    ImmutableError  // Attempted to modify immutable domain (e.g., VSS)
};

// ============================================================================
// DESIGN DB OBSERVER
// ============================================================================

class DesignDbObserver {
public:
    virtual ~DesignDbObserver() = default;
    
    // Incremental Signals
    virtual void onNetCreated(const Net& net) {}
    virtual void onNetModified(const Net& net) {} // e.g. Pin added
    virtual void ofInstancePlaced(const Instance& inst) {}
    virtual void onInstanceMoved(const Instance& inst) {}
    
    // Bulk Signals
    virtual void onTopologyInvalidated() {} // Requires full re-stamp
    virtual void onNetRenamed(NetUUID id, const std::string& oldName, const std::string& newName) {}
};

// ============================================================================
// HIERARCHICAL SNAPSHOT (Phase 5)
// ============================================================================

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

/**
 * @brief Captures a complete hierarchical view of a design for simulation.
 * Includes the top-level block and all resolved subcircuit blocks.
 * Note: Uses shared_ptr to TensorBlock to work with forward declaration.
 */
struct HierarchicalSnapshot {
    std::shared_ptr<TensorBlock> topBlock;                              // Top-level flattened netlist
    std::map<std::string, std::shared_ptr<TensorBlock>> subBlocks;      // Subcircuit name -> TensorBlock
    
    // Caching Invariant: One snapshot per unique (path, parameters)
    struct CacheKey {
        std::string path;
        std::map<std::string, double> params;
        bool operator<(const CacheKey& other) const {
            if (path != other.path) return path < other.path;
            return params < other.params;
        }
    };
    std::map<CacheKey, std::shared_ptr<TensorBlock>> subBlockCache;

    std::vector<std::string> loadOrder;                                 // Order in which subcircuits were loaded
    int maxDepthReached = 0;                                            // Actual depth traversed
};

class DesignDb {
public:
    // Core CRUD
    NetUUID createNet(const std::string& name);
    MutationResult renameNet(NetUUID id, const std::string& newName);
    MutationResult renameNet(const std::string& oldName, const std::string& newName); // Helper
    InstUUID createInstance(const std::string& master, const std::string& name);

    // Connectivity
    PinUUID connectPin(InstUUID inst, int terminal, NetUUID net);
    void disconnectPin(PinUUID pin);

    // Parameter Management
    void setParam(InstUUID inst, const std::string& key, double value);
    void setStringParam(InstUUID inst, const std::string& key, const std::string& value);
    void clearPins(InstUUID inst);
    void clear();

    // Queries
    const Net* getNet(NetUUID id) const;
    std::string getNetName(NetUUID id) const {
        auto it = nets_.find(id);
        return (it != nets_.end()) ? it->second.name : "UNKNOWN";
    }
    const std::map<NetUUID, Net>& getAllNets() const { return nets_; }

    // Power Domain Management
    PowerDomainRegistry& getPowerDomains() { return powerDomainRegistry_; }
    const PowerDomainRegistry& getPowerDomains() const { return powerDomainRegistry_; }
    // Mutable Access (Paradigm Bridge - careful!)
    Instance* getInstanceMutable(InstUUID id);
    const Instance* getInstance(InstUUID id) const;
    const Pin* getPin(PinUUID id) const;

    // Snapshot Generation (Paradigm Bridge)
    // Generates an immutable TensorBlock for the solver
    TensorBlock snapshot() const;

    // Extended snapshot that also returns the NetUUID->node index mapping
    // Used by generateNetlist() to build BlockInstance nodeMapping
    struct SnapshotResult {
        std::shared_ptr<TensorBlock> block;
        std::map<NetUUID, int> netToNode;
    };
    SnapshotResult snapshotWithNodeMap() const;

    /**
     * @brief Generate a hierarchical snapshot by recursively resolving subcircuits.
     * @param resolver Function that maps a master/subcircuit name and its parameters to a TensorBlock.
     *                 Returns nullptr if the subcircuit cannot be resolved (primitive).
     * @param maxDepth Maximum recursion depth (default 16 to prevent infinite loops).
     * @return HierarchicalSnapshot containing all resolved blocks.
     */
    HierarchicalSnapshot recursiveSnapshot(
        std::function<std::shared_ptr<TensorBlock>(const std::string& masterName, const std::map<std::string, double>& params)> resolver,
        int maxDepth = 16
    ) const;

    // Observer Registry
    void addObserver(DesignDbObserver* obs);
    void removeObserver(DesignDbObserver* obs);

private:
    // Storage
    std::map<NetUUID, Net> nets_;
    std::map<InstUUID, Instance> instances_;
    std::map<PinUUID, Pin> pins_;
    
    // Observers
    std::vector<DesignDbObserver*> observers_;
    
    // Power Domain Registry (Phase 1.3)
    PowerDomainRegistry powerDomainRegistry_;
    
    // Helper
    void notifyTopologyChanged() {
        for (auto* obs : observers_) obs->onTopologyInvalidated();
    }
};

} // namespace components
} // namespace acutesim
