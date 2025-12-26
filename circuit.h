#pragma once
#include <vector>
#include <string>

/**
 * @file circuit.h
 * @brief Circuit Netlist - Component definitions and hierarchical storage.
 * 
 * ============================================================================
 * IMPLEMENTATION GUIDE: TENSOR MNA HIERARCHY
 * ============================================================================
 * 
 * The goal is to represent circuits as a collection of reusable "Tensor Blocks"
 * rather than a flat list of components. This enables:
 *   1. Structural Reuse: Identical blocks share symbolic factorization.
 *   2. Batched Solving: N identical instances can be solved as one Tensor.
 *   3. VLSI Scale: Millions of gates become thousands of unique blocks.
 * 
 * KEY ABSTRACTIONS:
 *   - TensorBlock: A template for a sub-circuit (e.g., NAND gate, SRAM cell).
 *   - TensorNetlist: The top-level container that holds block definitions
 *                    and their instances in the global circuit.
 *   - BlockInstance: A single placement of a TensorBlock, with a node mapping
 *                    from the block's local nodes to global netlist nodes.
 */

// ============================================================================
// COMPONENT DEFINITIONS (Unchanged from flat MNA)
// ============================================================================

struct Resistor 
{
    int nodePos;
    int nodeNeg;
    double resistance;
};

struct Capacitor 
{
    int nodePos;
    int nodeNeg;
    double capacitance;
    double v_prev = 0.0;
    double i_prev = 0.0;
};

struct Inductor 
{
    int nodePos;
    int nodeNeg;
    double inductance;
    double v_prev = 0.0;
    double i_prev = 0.0;
};

struct VoltageSource 
{
    int nodePos;
    int nodeNeg;
    double voltage;
};

struct CurrentSource 
{
    int nodePos;
    int nodeNeg;
    double current;
};

struct Diode
{
    int nodePos;
    int nodeNeg;
    double leakage_current;
    double thermal_voltage;
}; 

// ============================================================================
// TODO: IMPLEMENT TensorBlock
// ============================================================================
/**
 * PSEUDO-CODE:
 * 
 * class TensorBlock {
 * public:
 *     std::string name;  // Unique identifier (e.g., "INV_X1", "NAND2_X2")
 *     
 *     // Component storage (local node indices, relative to block's ground)
 *     std::vector<Resistor> resistors;
 *     std::vector<Capacitor> capacitors;
 *     // ... other components
 *     
 *     int numInternalNodes = 0;  // Max local node index used in this block
 *     
 *     // Add component methods (update numInternalNodes as components are added)
 *     void addResistor(int localNodePos, int localNodeNeg, double R);
 *     void addCapacitor(int localNodePos, int localNodeNeg, double C);
 *     // ...
 * };
 */

// ============================================================================
// TODO: IMPLEMENT BlockInstance
// ============================================================================
/**
 * PSEUDO-CODE:
 * 
 * struct BlockInstance {
 *     std::string blockName;       // Which TensorBlock template this is
 *     std::vector<int> nodeMapping; // Maps local node index -> global node index
 *                                   // nodeMapping[0] = global node for local node 1
 *                                   // (local node 0 is always ground)
 * };
 */

// ============================================================================
// TODO: IMPLEMENT TensorNetlist
// ============================================================================
/**
 * PSEUDO-CODE:
 * 
 * class TensorNetlist {
 * public:
 *     // Block Definitions (Library of reusable sub-circuits)
 *     std::vector<TensorBlock> blockDefinitions;
 *     
 *     // Global Components (Top-level, non-hierarchical parts)
 *     TensorBlock globalBlock;
 *     
 *     // Hierarchical Instances
 *     std::vector<BlockInstance> instances;
 *     
 *     int numGlobalNodes = 0;
 *     
 *     // Add a block definition to the library
 *     void defineBlock(const TensorBlock& block) {
 *         blockDefinitions.push_back(block);
 *     }
 *     
 *     // Place an instance of a block in the netlist
 *     // nodeMap[i] is the global node for local node (i+1) of the block
 *     void addInstance(const std::string& blockName, const std::vector<int>& nodeMap) {
 *         instances.push_back({blockName, nodeMap});
 *         for (int globalNode : nodeMap) {
 *             if (globalNode > numGlobalNodes) numGlobalNodes = globalNode;
 *         }
 *     }
 *     
 *     // Convenience methods for adding global (top-level) components
 *     void addResistor(int nPos, int nNeg, double R);
 *     void addCapacitor(int nPos, int nNeg, double C);
 *     void addVoltageSource(int nPos, int nNeg, double V);
 * };
 */
