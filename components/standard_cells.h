#pragma once
#include "circuit.h"
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
inline SubCircuitDefinition createULN2003A() {
    SubCircuitDefinition def;
    def.name = "ULN2003A";
    def.description = "7-Channel Darlington Transistor Array with Clamp Diodes";
    def.pinCount = 16;
    def.pinNames = {
        "1B", "2B", "3B", "4B", "5B", "6B", "7B",  // Inputs
        "GND",                                      // Pin 8
        "COM",                                      // Pin 9 (diode common)
        "7C", "6C", "5C", "4C", "3C", "2C", "1C"   // Outputs (reversed)
    };
    
    TensorBlock& block = def.internalBlock;
    block.name = "ULN2003A_INTERNAL";
    
    // Pin-to-internal node mapping:
    // External pins 1-7 (inputs) -> internal nodes 1-7
    // External pin 8 (GND) -> internal node 0 (ground reference)
    // External pin 9 (COM) -> internal node 8
    // External pins 10-16 (outputs 7C-1C) -> internal nodes 9-15
    
    def.pinToInternalNode[0] = 1;   // 1B -> node 1
    def.pinToInternalNode[1] = 2;   // 2B -> node 2
    def.pinToInternalNode[2] = 3;   // 3B -> node 3
    def.pinToInternalNode[3] = 4;   // 4B -> node 4
    def.pinToInternalNode[4] = 5;   // 5B -> node 5
    def.pinToInternalNode[5] = 6;   // 6B -> node 6
    def.pinToInternalNode[6] = 7;   // 7B -> node 7
    def.pinToInternalNode[7] = 0;   // GND -> node 0
    def.pinToInternalNode[8] = 8;   // COM -> node 8
    def.pinToInternalNode[9] = 15;  // 7C -> node 15
    def.pinToInternalNode[10] = 14; // 6C -> node 14
    def.pinToInternalNode[11] = 13; // 5C -> node 13
    def.pinToInternalNode[12] = 12; // 4C -> node 12
    def.pinToInternalNode[13] = 11; // 3C -> node 11
    def.pinToInternalNode[14] = 10; // 2C -> node 10
    def.pinToInternalNode[15] = 9;  // 1C -> node 9
    
    // Internal node allocation:
    // Nodes 1-7: Input pins
    // Node 0: GND
    // Node 8: COM
    // Nodes 9-15: Output pins (1C-7C)
    // Nodes 16-22: Q1 emitter / Q2 base (internal Darlington connection)
    
    int nextInternalNode = 16;
    
    // Create 7 Darlington channels
    for (int ch = 0; ch < 7; ++ch) {
        int inputNode = ch + 1;      // 1B is node 1, etc.
        int outputNode = 9 + ch;     // 1C is node 9, etc.
        int q1q2Node = nextInternalNode++;  // Internal connection Q1.E <-> Q2.B
        
        // Input Resistor: 2.7kΩ from input to Q1 base
        int q1BaseNode = nextInternalNode++;
        block.addResistor("R_IN_" + std::to_string(ch+1), inputNode, q1BaseNode, 2700.0);
        
        // Q1: First transistor of Darlington
        // Collector -> GND (or actually, to output for sink current? No, for Darlington sink:
        //   Q1: C=Output, B=inputResistor, E=Q2.B
        //   Q2: C=Output, B=Q1.E, E=GND
        // Wait, ULN2003 is a current sink. Output is open-collector.
        // Q1.C and Q2.C tied together -> Output
        // Q1.E -> Q2.B
        // Q2.E -> GND
        block.addBJT("Q1_" + std::to_string(ch+1), 
                     outputNode,    // Collector -> Output
                     q1BaseNode,    // Base <- Input resistor
                     q1q2Node,      // Emitter -> Q2 Base
                     "npn_generic", true, 
                     100.0, 1.0, 1e-14, 0.02585);
        
        block.addBJT("Q2_" + std::to_string(ch+1),
                     outputNode,    // Collector -> Output
                     q1q2Node,      // Base <- Q1 Emitter
                     0,             // Emitter -> GND
                     "npn_generic", true,
                     100.0, 1.0, 1e-14, 0.02585);
        
        // Clamp Diode: Output -> COM (for flyback protection)
        // When output goes below GND (inductive kickback), diode clamps to COM
        block.addDiode(8, outputNode, 1e-14);  // COM is anode, Output is cathode
    }
    
    block.numInternalNodes = nextInternalNode;
    
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
        auto it = definitions_.find(name);
        return (it != definitions_.end()) ? &it->second : nullptr;
    }
    
    void registerDefinition(const SubCircuitDefinition& def) {
        definitions_[def.name] = def;
    }
    
    bool contains(const std::string& name) const {
        return definitions_.find(name) != definitions_.end();
    }
    
private:
    StandardCellLibrary() {
        // Register built-in ICs
        registerDefinition(createULN2003A());
        // Future: registerDefinition(createNE555());
        // Future: registerDefinition(createLM7805());
    }
    
    std::map<std::string, SubCircuitDefinition> definitions_;
};

} // namespace StandardCells
