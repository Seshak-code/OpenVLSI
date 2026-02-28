#pragma once
#include "acutesim_engine/netlist/circuit.h"
#include "../compute/orchestration/dto/schematic_dto.h"
#include <map>
#include <string>

/*
 * standard_cells.h
 * Standard Cell Library - PDK-compliant IC definitions.
 *
 * Each IC is defined as a TensorBlock containing primitive devices.
 * This allows simulation at the transistor level while maintaining
 * IC-level abstraction in the GUI.
 *
 * PDK Compliance:
 * - All BJTs use generic model names (e.g., "npn_generic")
 * - Future: Map to SKY130/GF180 specific models via PDK binding layer
 */

namespace StandardCells {

/**
 * createULN2003A
 * 
 * ULN2003A: 7-channel Darlington transistor array with integral
 * suppression diodes for driving relays, motors, solenoids.
 *
 * Package: DIP-16
 * Pins:
 *   1-7:   Inputs (1B-7B)
 *   8:     GND
 *   9:     COM (common cathode for clamp diodes, typically to Vcc of load)
 *   10-16: Outputs (7C-1C, reversed order)
 *
 * Internal per-channel:
 *   Input -> 2.7kΩ -> Darlington pair (Q1->Q2) -> Output
 *   Output -> Clamp diode -> COM
 * 
 * Darlington: Q1 collector tied to Q2 base, provides ~2000x gain.
 */
/**
 * createGenericDarlington
 * 
 * A generic, physics-based Darlington Pair.
 * Structure:
 *   - Q1: Driver Transistor
 *   - Q2: Output Transistor
 *   - R1: Bias Resistor (Base-Emitter of Q1) - Optional, included for stability
 *   - R2: Bias Resistor (Base-Emitter of Q2) - Optional, included for stability
 *
 * Topology:
 *   Collector (C) -> Q1.C, Q2.C
 *   Base (B)      -> Q1.B
 *   Emitter (E)   -> Q2.E
 *   Internal      -> Q1.E connected to Q2.B
 * 
 * Pins: "B", "C", "E"
 */
inline SubCircuitDefinition createGenericDarlington() {
    SubCircuitDefinition def;
    def.name = "Darlington Pair";
    def.description = "Generic NPN Darlington Pair (High Gain)";
    def.pinCount = 3;
    def.pinNames = {"B", "C", "E"};
    
    TensorBlock& block = def.internalBlock;
    block.name = "DARLINGTON_INTERNAL";
    block.pinNames = def.pinNames; // Maps pins to internal nodes 1, 2, 3
    
    // External Pin Mapping
    // 0: Base
    // 1: Collector
    // 2: Emitter
    
    // Internal Node Mapping
    // 0: Local Ground (Reference, unused if floating, but good practice to map E to logic GND if needed)
    // Let's use 1-based indexing for signals and map them.
    // Node 1: Base
    // Node 2: Collector
    // Node 3: Emitter
    // Node 4: Internal Q1E_Q2B
    
    def.pinToInternalNode[0] = 1; // Base
    def.pinToInternalNode[1] = 2; // Collector
    def.pinToInternalNode[2] = 3; // Emitter
    
    // Internal components
    // Q1: NPN
    //   C: Node 2
    //   B: Node 1
    //   E: Node 4
    // Q2: NPN
    //   C: Node 2
    //   B: Node 4
    //   E: Node 3
    
    block.addBJT("Q1", 2, 1, 4, "npn_generic", true, 100.0, 1.0, 1e-14);
    block.addBJT("Q2", 2, 4, 3, "npn_generic", true, 100.0, 1.0, 1e-14);
    
    // Optional Stability Resistors (simulating leakage paths or commercial integration)
    // Removed for stability debugging - starting with pure Darlington
    // block.addResistor("R1", 1, 4, 8000.0);
    // block.addResistor("R2", 4, 3, 120.0);
    
    block.numInternalNodes = 4;
    return def;
}

/**
 * createResistiveDivider
 * A simple 1:1 voltage divider using two 10k resistors.
 * Pins: VIN, VOUT, GND
 */
inline SubCircuitDefinition createResistiveDivider() {
    SubCircuitDefinition def;
    def.name = "Resistive Divider (10k:10k)";
    def.description = "Precision 1:1 Voltage Divider (10k/10k)";
    def.pinCount = 3;
    def.pinNames = {"VIN", "VOUT", "GND"};
    
    // Pin Mapping: VIN(1), VOUT(2), GND(3 -> maps to internal 0)
    // Note: We map external GND pin to internal Node 0 for convenience
    def.pinToInternalNode[0] = 1; // VIN
    def.pinToInternalNode[1] = 2; // VOUT
    def.pinToInternalNode[2] = 0; // GND -> Internal 0
    
    TensorBlock& block = def.internalBlock;
    block.name = "DIVIDER_INTERNAL";
    block.pinNames = def.pinNames;
    block.addResistor("R1", 1, 2, 10000.0);
    block.addResistor("R2", 2, 0, 10000.0);
    
    block.numInternalNodes = 2;
    return def;
}

/**
 * createHalfWaveRectifier
 * AC to DC rectifier using a generic diode and load resistor.
 * Pins: VIN, VOUT, GND
 */
inline SubCircuitDefinition createHalfWaveRectifier() {
    SubCircuitDefinition def;
    def.name = "Half-Wave Rectifier";
    def.description = "Simple Diode Rectifier with 100k Load";
    def.pinCount = 3;
    def.pinNames = {"VIN", "VOUT", "GND"};
    
    def.pinToInternalNode[0] = 1; // VIN
    def.pinToInternalNode[1] = 2; // VOUT
    def.pinToInternalNode[2] = 0; // GND
    
    TensorBlock& block = def.internalBlock;
    block.name = "RECTIFIER_INTERNAL";
    block.pinNames = def.pinNames;
    block.addDiode(1, 2, 1e-12); // D1: VIN -> VOUT
    block.diodes.back().modelName = "diode_pw2dw"; // Generic PDK diode
    block.addResistor(2, 0, 100000.0); // R_LOAD: 100k
    block.resistors.back().model_name = "poly_res_high";
    
    block.numInternalNodes = 2;
    return def;
}

/**
 * createCMOSInverter
 * Standard Digital Logic Inverter (NOT Gate).
 * Pins: VIN, VOUT, VDD, GND
 */
inline SubCircuitDefinition createCMOSInverter() {
    SubCircuitDefinition def;
    def.name = "CMOS Inverter";
    def.description = "High-speed Digital Inverter (NOT Gate)";
    def.pinCount = 4;
    def.pinNames = {"VIN", "VOUT", "VDD", "GND"};
    
    def.pinToInternalNode[0] = 1; // VIN
    def.pinToInternalNode[1] = 2; // VOUT
    def.pinToInternalNode[2] = 3; // VDD
    def.pinToInternalNode[3] = 0; // GND
    
    TensorBlock& block = def.internalBlock;
    block.name = "INVERTER_INTERNAL";
    block.pinNames = def.pinNames;
    
    // NMOS: VOUT pulled to GND when VIN is High
    // D=2, G=1, S=0, B=0
    // Using PDK_NFET primitive mapping
    block.addMosfet("M1_NMOS", 2, 1, 0, 0, "nfet_01v8", 2e-6, 0.18e-6);
    
    // PMOS: VOUT pulled to VDD when VIN is Low
    // D=2, G=1, S=3, B=3 (Body tied to VDD)
    // Using PDK_PFET primitive mapping
    block.addMosfet("M2_PMOS", 2, 1, 3, 3, "pfet_01v8", 4e-6, 0.18e-6);
    
    // High-Z Bias Resistors (100Meg) to prevent floating gate errors in unconnected state
    block.addResistor("R_BIAS_GND", 1, 0, 1e8);
    block.addResistor("R_BIAS_VDD", 3, 0, 1e8);
    
    block.numInternalNodes = 3;
    return def;
}

/**
 * createDifferentialPair
 * NPN Differential Amplifier pair with tail resistor.
 * Pins: IN+, IN-, OUT+, OUT-, VCC, GND
 */
inline SubCircuitDefinition createDifferentialPair() {
    SubCircuitDefinition def;
    def.name = "BJT Differential Pair";
    def.description = "Analog Differential Amplifier Stage";
    def.pinCount = 6;
    def.pinNames = {"IN+", "IN-", "OUT+", "OUT-", "VCC", "GND"};
    
    def.pinToInternalNode[0] = 1; // IN+
    def.pinToInternalNode[1] = 2; // IN-
    def.pinToInternalNode[2] = 4; // OUT+ (Collector Q2, right)
    def.pinToInternalNode[3] = 3; // OUT- (Collector Q1, left)
    def.pinToInternalNode[4] = 6; // VCC
    def.pinToInternalNode[5] = 0; // GND
    
    // Internal Node 5: Common Emitter Tail
    
    TensorBlock& block = def.internalBlock;
    block.name = "DIFFPAIR_INTERNAL";
    block.pinNames = def.pinNames;
    
    // Q1 (Left): C=3, B=1, E=5
    block.addBJT("Q1", 3, 1, 5, "npn_generic", true, 100.0, 1.0, 1e-14);
    
    // Q2 (Right): C=4, B=2, E=5
    block.addBJT("Q2", 4, 2, 5, "npn_generic", true, 100.0, 1.0, 1e-14);
    
    // Collector Loads (10k)
    block.addResistor("RC1", 6, 3, 10000.0);
    block.addResistor("RC2", 6, 4, 10000.0);
    
    // Tail Resistor (5k)
    block.addResistor("RE", 5, 0, 5000.0);
    
    // Input Bias (50k)
    block.addResistor("RB1", 1, 0, 50000.0);
    block.addResistor("RB2", 2, 0, 50000.0);
    
    block.numInternalNodes = 6;
    return def;
}

/**
 * createRCLowPass
 * First-order RC Filter (fc = 15.9 kHz).
 * Pins: VIN, VOUT, GND
 */
inline SubCircuitDefinition createRCLowPass() {
    SubCircuitDefinition def;
    def.name = "RC Low-Pass Filter";
    def.description = "Passive Filter (R=10k, C=1nF, fc=16kHz)";
    def.pinCount = 3;
    def.pinNames = {"VIN", "VOUT", "GND"};
    
    def.pinToInternalNode[0] = 1; // VIN
    def.pinToInternalNode[1] = 2; // VOUT
    def.pinToInternalNode[2] = 0; // GND
    
    TensorBlock& block = def.internalBlock;
    block.name = "RC_INTERNAL";
    block.pinNames = def.pinNames;
    
    block.addResistor("R1", 1, 2, 10000.0);
    block.resistors.back().model_name = "poly_res";
    block.addCapacitor(2, 0, 1e-9);
    block.capacitors.back().model_name = "mim_cap";
    
    block.numInternalNodes = 2;
    return def;
}

/**
 * createBiCMOSBuffer
 * High-impedance NMOS input driving low-impedance BJT output.
 * Pins: VIN, VOUT, VDD, GND
 */
inline SubCircuitDefinition createBiCMOSBuffer() {
    SubCircuitDefinition def;
    def.name = "BiCMOS Buffer";
    def.description = "NMOS Input + NPN Emitter Follower Output";
    def.pinCount = 4;
    def.pinNames = {"VIN", "VOUT", "VDD", "GND"};
    
    def.pinToInternalNode[0] = 1; // VIN
    def.pinToInternalNode[1] = 3; // VOUT (Emitter of Q1)
    def.pinToInternalNode[2] = 4; // VDD
    def.pinToInternalNode[3] = 0; // GND
    
    TensorBlock& block = def.internalBlock;
    block.name = "BICMOS_INTERNAL";
    block.pinNames = def.pinNames;
    
    // M1 (NMOS Input): D=2, G=1, S=0
    block.addMosfet("M1", 2, 1, 0, 0, "nmos_generic", 10e-6, 0.5e-6);
    
    // Q1 (NPN Output): C=4[VDD], B=2, E=3[VOUT]
    block.addBJT("Q1", 4, 2, 3, "npn_generic", true, 150.0); // Beta=150
    
    // Loads
    block.addResistor("R_DRAIN", 4, 2, 10000.0); // 10k Pull-up for NMOS
    block.addResistor("R_LOAD", 3, 0, 5000.0);   // 5k Emitter Load
    
    // Input Bias
    block.addResistor("R_BIAS", 1, 0, 100000.0);
    
    block.numInternalNodes = 4;
    return def;
}

/**
 * createPDK_NFET
 * Generic 4-terminal NMOS template for PDK mapping.
 * Pins: D, G, S, B
 */
inline SubCircuitDefinition createPDK_NFET() {
    SubCircuitDefinition def;
    def.name = "PDK_NFET";
    def.description = "Generic 4-Terminal NMOS (PDK Template)";
    def.pinCount = 4;
    def.pinNames = {"D", "G", "S", "B"};
    
    def.pinToInternalNode[0] = 1; // Drain
    def.pinToInternalNode[1] = 2; // Gate
    def.pinToInternalNode[2] = 3; // Source
    def.pinToInternalNode[3] = 4; // Body
    
    TensorBlock& block = def.internalBlock;
    block.name = "PDK_NFET_INTERNAL";
    block.pinNames = def.pinNames;
    block.addMosfet("M1", 1, 2, 3, 4, "nmos_generic", 1.0e-6, 0.18e-6);
    
    block.numInternalNodes = 4;
    return def;
}

/**
 * createPDK_PFET
 * Generic 4-terminal PMOS template for PDK mapping.
 * Pins: D, G, S, B
 */
inline SubCircuitDefinition createPDK_PFET() {
    SubCircuitDefinition def;
    def.name = "PDK_PFET";
    def.description = "Generic 4-Terminal PMOS (PDK Template)";
    def.pinCount = 4;
    def.pinNames = {"D", "G", "S", "B"};
    
    def.pinToInternalNode[0] = 1; // Drain
    def.pinToInternalNode[1] = 2; // Gate
    def.pinToInternalNode[2] = 3; // Source
    def.pinToInternalNode[3] = 4; // Body
    
    TensorBlock& block = def.internalBlock;
    block.name = "PDK_PFET_INTERNAL";
    block.pinNames = def.pinNames;
    block.addMosfet("M1", 1, 2, 3, 4, "pmos_generic", 1.0e-6, 0.18e-6);
    
    block.numInternalNodes = 4;
    return def;
}

/**
 * createPDK_Resistor_Poly
 * Generic Polysilicon Resistor with substrate terminal.
 * Pins: A, B, SUB
 */
inline SubCircuitDefinition createPDK_Resistor_Poly() {
    SubCircuitDefinition def;
    def.name = "PDK_Resistor_Poly";
    def.description = "Generic Poly Resistor with Substrate Terminal";
    def.pinCount = 3;
    def.pinNames = {"A", "B", "SUB"};
    
    def.pinToInternalNode[0] = 1; // Terminal A
    def.pinToInternalNode[1] = 2; // Terminal B
    def.pinToInternalNode[2] = 3; // Substrate
    
    TensorBlock& block = def.internalBlock;
    block.name = "PDK_RES_POLY_INTERNAL";
    block.pinNames = def.pinNames;
    block.addResistor("R1", 1, 2, 1000.0);
    block.resistors.back().model_name = "poly_res_generic";
    block.resistors.back().width_m = 1.0e-6;
    block.resistors.back().length_m = 10.0e-6;
    
    // Parasitic capacitance to substrate (simplified)
    block.addCapacitor(1, 3, 1e-15); 
    block.addCapacitor(2, 3, 1e-15);
    
    block.numInternalNodes = 3;
    return def;
}

/**
 * createPDK_Capacitor_MIM
 * Generic Metal-Insulator-Metal Capacitor.
 * Pins: TOP, BOT
 */
inline SubCircuitDefinition createPDK_Capacitor_MIM() {
    SubCircuitDefinition def;
    def.name = "PDK_Capacitor_MIM";
    def.description = "Generic MIM Capacitor";
    def.pinCount = 2;
    def.pinNames = {"TOP", "BOT"};
    
    def.pinToInternalNode[0] = 1; // Top Plate
    def.pinToInternalNode[1] = 2; // Bottom Plate
    
    TensorBlock& block = def.internalBlock;
    block.name = "PDK_CAP_MIM_INTERNAL";
    block.pinNames = def.pinNames;
    block.addCapacitor(1, 2, 1e-12); // 1pF Default
    block.capacitors.back().model_name = "mim_cap_generic";
    block.capacitors.back().width_m = 10.0e-6;
    block.capacitors.back().length_m = 10.0e-6;
    
    block.numInternalNodes = 2;
    return def;
}



/**
 * StandardCellLibrary
 * 
 * Registry of all available IC subcircuits.
 * Lazily populates definitions on first access.
 */
class StandardCellLibrary {
public:
    static StandardCellLibrary& instance() {
        static StandardCellLibrary lib;
        return lib;
    }

    const SubCircuitDefinition* get(const std::string& name) const {
        // Data-driven cells take priority over hardcoded fallbacks
        auto it = dataDrivenDefs_.find(name);
        if (it != dataDrivenDefs_.end()) return &it->second;
        auto it2 = definitions_.find(name);
        return (it2 != definitions_.end()) ? &it2->second : nullptr;
    }

    std::optional<acutesim::compute::orchestration::SchematicGraphDTO> getDto(const std::string& name) const {
        const auto* def = get(name);
        if (!def) return std::nullopt;
        
        using namespace acutesim::compute::orchestration;
        SchematicGraphDTO dto;
        dto.name = def->name;
        dto.portNames = def->pinNames;
        
        const auto& block = def->internalBlock;
        
        // Map internal nodes to DTO nets
        for (int i = 1; i <= block.numInternalNodes; ++i) {
            SchematicNodeDTO net;
            net.name = "n" + std::to_string(i);
            net.index = i;
            dto.nets.push_back(net);
        }
        
        // Converters for instances
        auto addInst = [&](const std::string& type, const std::string& name, const std::vector<int>& nodes, const std::map<std::string, std::string>& params = {}) {
            SchematicInstanceDTO inst;
            inst.type = type;
            inst.instanceName = name;
            for (int n : nodes) {
                if (n == 0) inst.connectedNets.push_back("0");
                else inst.connectedNets.push_back("n" + std::to_string(n));
            }
            inst.parameters = params;
            dto.instances.push_back(inst);
        };

        for (const auto& r : block.resistors) addInst("Resistor", r.name, {r.nodeTerminal1, r.nodeTerminal2}, {{"R", std::to_string(r.resistance_ohms)}});
        for (const auto& c : block.capacitors) addInst("Capacitor", c.name, {c.nodePlate1, c.nodePlate2}, {{"C", std::to_string(c.capacitance_farads)}});
        for (const auto& l : block.inductors) addInst("Inductor", l.name, {l.nodeCoil1, l.nodeCoil2}, {{"L", std::to_string(l.inductance_henries)}});
        for (const auto& m : block.mosfets) addInst("Mosfet", m.instanceName, {m.drain, m.gate, m.source, m.body}, {{"model", m.modelName}, {"W", std::to_string(m.w)}, {"L", std::to_string(m.l)}});
        for (const auto& q : block.bjts) addInst("BJT", q.instanceName, {q.nodeCollector, q.base, q.emitter}, {{"isNPN", q.isNPN ? "1" : "0"}, {"Is", std::to_string(q.saturationCurrent_I_S_A)}});
        for (const auto& d : block.diodes) addInst("Diode", d.instanceName, {d.anode, d.cathode}, {{"Is", std::to_string(d.saturationCurrent_I_S_A)}});

        return dto;
    }

    void registerDefinition(const SubCircuitDefinition& def) {
        definitions_[def.name] = def;
    }

    bool contains(const std::string& name) const {
        return dataDrivenDefs_.find(name) != dataDrivenDefs_.end()
            || definitions_.find(name) != definitions_.end();
    }

    std::vector<std::string> getNames() const {
        std::vector<std::string> names;
        // Data-driven cells first
        for (const auto& kv : dataDrivenDefs_) {
            names.push_back(kv.first);
        }
        // Hardcoded fallbacks (skip duplicates)
        for (const auto& kv : definitions_) {
            if (dataDrivenDefs_.find(kv.first) == dataDrivenDefs_.end())
                names.push_back(kv.first);
        }
        return names;
    }

    /**
     * Load all .sch files from a directory and register them as data-driven cells.
     * Returns the number of cells successfully loaded.
     * Implemented in standard_cells.cpp.
     */
    int loadFromDirectory(const std::string& dir);

    /**
     * Parse a single .sch JSON file into a SubCircuitDefinition.
     * Returns true on success, populating outDef.
     * Implemented in standard_cells.cpp.
     */
    static bool loadCellFromSchematic(const std::string& path, SubCircuitDefinition& outDef);

private:
    StandardCellLibrary() {
        // Register built-in ICs (hardcoded fallbacks)
        registerDefinition(createGenericDarlington());
        registerDefinition(createResistiveDivider());
        registerDefinition(createHalfWaveRectifier());
        registerDefinition(createCMOSInverter());
        registerDefinition(createDifferentialPair());
        registerDefinition(createRCLowPass());
        registerDefinition(createBiCMOSBuffer());

        // PDK-Ready Templates
        registerDefinition(createPDK_NFET());
        registerDefinition(createPDK_PFET());
        registerDefinition(createPDK_Resistor_Poly());
        registerDefinition(createPDK_Capacitor_MIM());
    }

    std::map<std::string, SubCircuitDefinition> definitions_;      // Hardcoded
    std::map<std::string, SubCircuitDefinition> dataDrivenDefs_;   // From .sch files
};

} // namespace StandardCells
