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
    double extractedC = 0.0; 
    double extractedR = 0.0;
};

/**
 * @brief Instance: A placed component (Resistor, MOSFET, Subcircuit).
 */
struct Instance {
    InstUUID id;
    std::string name;       // "R1", "M1"
    std::string masterName; // "nfet_01v8", "res_generic"
    
    // Parameter Blob (Key-Value)
    std::map<std::string, double> parameters;
    std::map<std::string, std::string> stringParameters;

    // Spatial Annotation (GUI only - NOT electrical truth)
    struct Spatial {
        double x, y;
        double rotation;
        bool flipped;
    } spatial;
};

// ============================================================================
// UNIFIED DESIGNDB (The Substrate)
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
};

class DesignDb {
public:
    // Core CRUD
    NetUUID createNet(const std::string& name);
    InstUUID createInstance(const std::string& master, const std::string& name);
    
    // Connectivity
    PinUUID connectPin(InstUUID inst, int terminal, NetUUID net);
    void disconnectPin(PinUUID pin);
    
    // Parameter Management
    void setParam(InstUUID inst, const std::string& key, double value);
    void setStringParam(InstUUID inst, const std::string& key, const std::string& value);
    void clearPins(InstUUID inst);
    
    // Queries
    const Net* getNet(NetUUID id) const;
    // ...
    // Mutable Access (Paradigm Bridge - careful!)
    Instance* getInstanceMutable(InstUUID id);
    const Instance* getInstance(InstUUID id) const;
    const Pin* getPin(PinUUID id) const;
    
    // Snapshot Generation (Paradigm Bridge)
    // Generates an immutable TensorBlock for the solver
    TensorBlock snapshot() const;
    
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
    
    // Helper
    void notifyTopologyChanged() {
        for (auto* obs : observers_) obs->onTopologyInvalidated();
    }
};

} // namespace components
} // namespace acutesim
