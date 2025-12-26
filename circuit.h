#pragma once
#include <vector>
#include <string>
#include <algorithm>

/**
 * @file circuit.h
 * @brief Circuit Netlist - Component definitions and hierarchical storage.
 */

// ============================================================================
// COMPONENT DEFINITIONS
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
// HIERARCHICAL DATA STRUCTURES
// ============================================================================

class TensorBlock {
public:
    std::string name;  // Unique identifier (e.g., "INV_X1", "NAND2_X2")
    
    // Component storage (local node indices, relative to block's ground)
    std::vector<Resistor> resistors;
    std::vector<Capacitor> capacitors;
    std::vector<Inductor> inductors;
    std::vector<VoltageSource> voltageSources;
    std::vector<CurrentSource> currentSources;
    std::vector<Diode> diodes;
    
    int numInternalNodes = 0;  // Max local node index used in this block
    
    void addResistor(int localNodePos, int localNodeNeg, double R) {
        resistors.push_back({localNodePos, localNodeNeg, R});
        updateNodeCount(localNodePos);
        updateNodeCount(localNodeNeg);
    }

    void addCapacitor(int localNodePos, int localNodeNeg, double C) {
        capacitors.push_back({localNodePos, localNodeNeg, C});
        updateNodeCount(localNodePos);
        updateNodeCount(localNodeNeg);
    }
    
    void addInductor(int localNodePos, int localNodeNeg, double L) {
        inductors.push_back({localNodePos, localNodeNeg, L});
        updateNodeCount(localNodePos);
        updateNodeCount(localNodeNeg);
    }

    void addVoltageSource(int localNodePos, int localNodeNeg, double V) {
        voltageSources.push_back({localNodePos, localNodeNeg, V});
        updateNodeCount(localNodePos);
        updateNodeCount(localNodeNeg);
    }
    
    // Helper to track max node index
    void updateNodeCount(int nodeIndex) {
        if (nodeIndex > numInternalNodes) numInternalNodes = nodeIndex;
    }
};

struct BlockInstance {
    std::string blockName;       // Which TensorBlock template this is
    std::vector<int> nodeMapping; // Maps local node index -> global node index
};

class TensorNetlist {
public:
    // Block Definitions (Library of reusable sub-circuits)
    std::vector<TensorBlock> blockDefinitions;
    
    // Global Components (Top-level, non-hierarchical parts)
    TensorBlock globalBlock;
    
    // Hierarchical Instances
    std::vector<BlockInstance> instances;
    
    int numGlobalNodes = 0;
    
    TensorNetlist() {
        globalBlock.name = "GLOBAL";
    }

    // Add a block definition to the library
    void defineBlock(const TensorBlock& block) {
        blockDefinitions.push_back(block);
    }
    
    // Place an instance of a block in the netlist
    void addInstance(const std::string& blockName, const std::vector<int>& nodeMap) {
        instances.push_back({blockName, nodeMap});
        for (int globalNode : nodeMap) {
            if (globalNode > numGlobalNodes) numGlobalNodes = globalNode;
        }
    }
    
    // Convenience methods for adding global (top-level) components
    void addResistor(int nPos, int nNeg, double R) {
        globalBlock.addResistor(nPos, nNeg, R);
        updateGlobalNodeCount(nPos);
        updateGlobalNodeCount(nNeg);
    }

    void addCapacitor(int nPos, int nNeg, double C) {
        globalBlock.addCapacitor(nPos, nNeg, C);
        updateGlobalNodeCount(nPos);
        updateGlobalNodeCount(nNeg);
    }

    void addVoltageSource(int nPos, int nNeg, double V) {
        globalBlock.addVoltageSource(nPos, nNeg, V);
        updateGlobalNodeCount(nPos);
        updateGlobalNodeCount(nNeg);
    }
    
    void updateGlobalNodeCount(int nodeIndex) {
        if (nodeIndex > numGlobalNodes) numGlobalNodes = nodeIndex;
    }
};
