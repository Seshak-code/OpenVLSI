#pragma once

#include "circuit.h"
#include "subcircuit_validator.h"
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <map>
#include <vector>

/**
 * component_library.h
 * 
 * Component library persistence layer for saving and loading subcircuit
 * definitions in the .comp file format.
 * 
 * Design Goals:
 * - JSON-based for human readability and git-friendliness
 * - AI API accessible for programmatic wiring and simulation
 * - Mode-aware (Bench/Block/System) with appropriate abstractions
 * - PDK hints for future layout integration
 * 
 * File Format Version: 1.0
 */

namespace fs = std::filesystem;

// ============================================================================
// PIN TYPE ENUM (for AI API wiring hints)
// ============================================================================

// ============================================================================
// PIN TYPE ENUM (for AI API wiring hints)
// ============================================================================

enum class PinType {
    SIGNAL,  // General I/O
    POWER,   // VDD/VCC
    GROUND,  // GND/VSS
    CLOCK,   // Clock input
    ANALOG,  // High-impedance analog
    DIGITAL  // Digital logic level
};

inline std::string pinTypeToString(PinType t) {
    switch (t) {
        case PinType::SIGNAL:  return "signal";
        case PinType::POWER:   return "power";
        case PinType::GROUND:  return "ground";
        case PinType::CLOCK:   return "clock";
        case PinType::ANALOG:  return "analog";
        case PinType::DIGITAL: return "digital";
    }
    return "signal";
}

inline PinType stringToPinType(const std::string& s) {
    if (s == "power") return PinType::POWER;
    if (s == "ground") return PinType::GROUND;
    if (s == "clock") return PinType::CLOCK;
    if (s == "analog") return PinType::ANALOG;
    if (s == "digital") return PinType::DIGITAL;
    return PinType::SIGNAL;
}

// ============================================================================
// EXTENDED PIN INFO (AI-accessible metadata)
// ============================================================================

struct LibraryPin {
    std::string name;          // "VDD", "IN", "OUT"
    PinType type = PinType::SIGNAL;
    int internalNode = -1;     // Maps to subcircuit internal node
    
    // AI API hints
    std::string direction;     // "input" | "output" | "inout" | "supply"
    double typicalVoltage = 0.0;  // Expected voltage (for AI wiring suggestions)
    std::string description;   // Human-readable purpose
};

// ============================================================================
// LIBRARY COMPONENT DEFINITION
// ============================================================================

struct LibraryComponent {
    // Identity
    std::string name;
    std::string description;
    std::string formatVersion = "1.0";
    
    // Package info
    std::string packageType = "DIP";  // DIP, QFN, BGA, CUSTOM
    int pinCount = 0;
    std::vector<LibraryPin> pins;
    
    // Internal circuitry
    SubCircuitDefinition subcircuit;
    
    // PDK hints (for future layout integration)
    std::string targetProcess;     // "sky130", "gf180", etc.
    double minSpacingUm = 0.0;     // Minimum feature spacing
    bool hasTunnelingConcerns = false;  // For advanced nodes
    
    // AI API metadata
    std::vector<std::string> tags;  // ["amplifier", "inverter", "buffer"]
    std::string category;           // "analog", "digital", "power", "io"
};

// ============================================================================
// SIMPLE JSON SERIALIZATION (No external dependencies)
// ============================================================================

namespace ComponentJSON {

// Escape string for JSON
inline std::string escape(const std::string& s) {
    std::string result;
    for (char c : s) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:   result += c;
        }
    }
    return result;
}

// Serialize resistors array
inline std::string serializeResistors(const std::vector<Resistor>& resistors) {
    std::ostringstream ss;
    ss << "[";
    for (size_t i = 0; i < resistors.size(); ++i) {
        const auto& r = resistors[i];
        ss << "{\"name\":\"" << escape(r.name) << "\","
           << "\"n1\":" << r.nodeTerminal1 << ","
           << "\"n2\":" << r.nodeTerminal2 << ","
           << "\"R\":" << r.resistance_ohms << "}";
        if (i < resistors.size() - 1) ss << ",";
    }
    ss << "]";
    return ss.str();
}

// Serialize capacitors array
inline std::string serializeCapacitors(const std::vector<Capacitor>& caps) {
    std::ostringstream ss;
    ss << "[";
    for (size_t i = 0; i < caps.size(); ++i) {
        const auto& c = caps[i];
        ss << "{\"n1\":" << c.nodePlate1 << ","
           << "\"n2\":" << c.nodePlate2 << ","
           << "\"C\":" << c.capacitance_farads << "}";
        if (i < caps.size() - 1) ss << ",";
    }
    ss << "]";
    return ss.str();
}

// Serialize diodes array
inline std::string serializeDiodes(const std::vector<Diode>& diodes) {
    std::ostringstream ss;
    ss << "[";
    for (size_t i = 0; i < diodes.size(); ++i) {
        const auto& d = diodes[i];
        ss << "{\"anode\":" << d.anode << ","
           << "\"cathode\":" << d.cathode << ","
           << "\"Is\":" << d.saturationCurrent_I_S_A << ","
           << "\"N\":" << d.emissionCoefficient_N << "}";
        if (i < diodes.size() - 1) ss << ",";
    }
    ss << "]";
    return ss.str();
}

// Serialize BJTs array
inline std::string serializeBJTs(const std::vector<BJT>& bjts) {
    std::ostringstream ss;
    ss << "[";
    for (size_t i = 0; i < bjts.size(); ++i) {
        const auto& q = bjts[i];
        ss << "{\"name\":\"" << escape(q.instanceName) << "\","
           << "\"C\":" << q.nodeCollector << ","
           << "\"B\":" << q.base << ","
           << "\"E\":" << q.emitter << ","
           << "\"model\":\"" << escape(q.modelName) << "\","
           << "\"isNPN\":" << (q.isNPN ? "true" : "false") << ","
           << "\"betaF\":" << q.betaF << ","
           << "\"Is\":" << q.saturationCurrent_I_S_A << "}";
        if (i < bjts.size() - 1) ss << ",";
    }
    ss << "]";
    return ss.str();
}

// Serialize MOSFETs array
inline std::string serializeMosfets(const std::vector<Mosfet>& mosfets) {
    std::ostringstream ss;
    ss << "[";
    for (size_t i = 0; i < mosfets.size(); ++i) {
        const auto& m = mosfets[i];
        ss << "{\"name\":\"" << escape(m.instanceName) << "\","
           << "\"D\":" << m.drain << ","
           << "\"G\":" << m.gate << ","
           << "\"S\":" << m.source << ","
           << "\"B\":" << m.body << ","
           << "\"model\":\"" << escape(m.modelName) << "\","
           << "\"W\":" << m.channelWidth_m << ","
           << "\"L\":" << m.channelLength_m << "}";
        if (i < mosfets.size() - 1) ss << ",";
    }
    ss << "]";
    return ss.str();
}

// Serialize pins array
inline std::string serializePins(const std::vector<LibraryPin>& pins) {
    std::ostringstream ss;
    ss << "[";
    for (size_t i = 0; i < pins.size(); ++i) {
        const auto& p = pins[i];
        ss << "{\"name\":\"" << escape(p.name) << "\","
           << "\"type\":\"" << pinTypeToString(p.type) << "\","
           << "\"node\":" << p.internalNode << ","
           << "\"dir\":\"" << escape(p.direction) << "\","
           << "\"desc\":\"" << escape(p.description) << "\"}";
        if (i < pins.size() - 1) ss << ",";
    }
    ss << "]";
    return ss.str();
}

} // namespace ComponentJSON

// ============================================================================
// SAVE COMPONENT TO FILE
// ============================================================================

inline bool saveComponent(const std::string& filepath, const LibraryComponent& comp) {
    std::ofstream file(filepath);
    if (!file.is_open()) return false;
    
    const auto& block = comp.subcircuit.internalBlock;
    
    file << "{\n";
    file << "  \"format_version\": \"" << comp.formatVersion << "\",\n";
    file << "  \"component\": {\n";
    file << "    \"name\": \"" << ComponentJSON::escape(comp.name) << "\",\n";
    file << "    \"description\": \"" << ComponentJSON::escape(comp.description) << "\",\n";
    file << "    \"package\": \"" << ComponentJSON::escape(comp.packageType) << "\",\n";
    file << "    \"category\": \"" << ComponentJSON::escape(comp.category) << "\",\n";
    
    // Pins (AI API accessible)
    file << "    \"pins\": " << ComponentJSON::serializePins(comp.pins) << ",\n";
    
    // Devices
    file << "    \"devices\": {\n";
    file << "      \"resistors\": " << ComponentJSON::serializeResistors(block.resistors) << ",\n";
    file << "      \"capacitors\": " << ComponentJSON::serializeCapacitors(block.capacitors) << ",\n";
    file << "      \"diodes\": " << ComponentJSON::serializeDiodes(block.diodes) << ",\n";
    file << "      \"bjts\": " << ComponentJSON::serializeBJTs(block.bjts) << ",\n";
    file << "      \"mosfets\": " << ComponentJSON::serializeMosfets(block.mosfets) << "\n";
    file << "    },\n";
    
    // PDK hints
    file << "    \"pdk_hints\": {\n";
    file << "      \"target_process\": \"" << ComponentJSON::escape(comp.targetProcess) << "\",\n";
    file << "      \"min_spacing_um\": " << comp.minSpacingUm << ",\n";
    file << "      \"tunneling_concerns\": " << (comp.hasTunnelingConcerns ? "true" : "false") << "\n";
    file << "    },\n";
    
    // Tags (AI API metadata)
    file << "    \"tags\": [";
    for (size_t i = 0; i < comp.tags.size(); ++i) {
        file << "\"" << ComponentJSON::escape(comp.tags[i]) << "\"";
        if (i < comp.tags.size() - 1) file << ", ";
    }
    file << "]\n";
    
    file << "  }\n";
    file << "}\n";
    
    return file.good();
}

// ============================================================================
// GET LIBRARY PATH
// ============================================================================

inline std::string getLibraryPath() {
    // Use ~/.acutesim/components/ as default library location
    const char* home = std::getenv("HOME");
    if (!home) home = ".";
    
    std::string libPath = std::string(home) + "/.acutesim/components";
    
    // Create directory if it doesn't exist
    std::error_code ec;
    fs::create_directories(libPath, ec);
    
    return libPath;
}

// ============================================================================
// LIST COMPONENTS IN LIBRARY
// ============================================================================

struct ComponentEntry {
    std::string name;
    std::string filepath;
    std::string category;
};

inline std::vector<ComponentEntry> listLibraryComponents() {
    std::vector<ComponentEntry> entries;
    
    std::string libPath = getLibraryPath();
    std::error_code ec;
    
    for (const auto& entry : fs::directory_iterator(libPath, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".comp") {
            ComponentEntry ce;
            ce.filepath = entry.path().string();
            ce.name = entry.path().stem().string();
            entries.push_back(ce);
        }
    }
    
    return entries;
}

// ============================================================================
// AI API INTERFACE (Black Box Access)
// ============================================================================

/**
 * ComponentLibraryAPI
 * 
 * Simplified interface for AI API to query and use components.
 * This is the "black box" interface mentioned in the design requirements.
 */
namespace ComponentLibraryAPI {

    // Get all available components as JSON (for AI context)
    inline std::string getAvailableComponentsJSON() {
        auto components = listLibraryComponents();
        std::ostringstream ss;
        ss << "[";
        for (size_t i = 0; i < components.size(); ++i) {
            ss << "{\"name\":\"" << ComponentJSON::escape(components[i].name) << "\","
               << "\"path\":\"" << ComponentJSON::escape(components[i].filepath) << "\"}";
            if (i < components.size() - 1) ss << ",";
        }
        ss << "]";
        return ss.str();
    }
    
    // Check if component exists
    inline bool componentExists(const std::string& name) {
        std::string path = getLibraryPath() + "/" + name + ".comp";
        return fs::exists(path);
    }
    
    // Get component file path
    inline std::string getComponentPath(const std::string& name) {
        return getLibraryPath() + "/" + name + ".comp";
    }

} // namespace ComponentLibraryAPI
