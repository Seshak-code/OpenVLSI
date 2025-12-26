#pragma once
#include "linalg.h"
#include "circuit.h"
#include <vector>
#include <cmath>
#include <algorithm>

/**
 * @file circuitsim.h
 * @brief Circuit Simulator - Physics and Solver Engine.
 */

// ============================================================================
// STAMP FUNCTIONS (Component -> Matrix Entry)
// ============================================================================

inline void stampResistor(int nodeI, int nodeJ, double resistance, MatrixConstructor& matrixBuilder)
{
    double g = 1.0 / resistance;
    if (nodeI != 0) {
        matrixBuilder.add(nodeI - 1, nodeI - 1, g);
        if (nodeJ != 0) matrixBuilder.add(nodeI - 1, nodeJ - 1, -g);
    }
    if (nodeJ != 0) {
        matrixBuilder.add(nodeJ - 1, nodeJ - 1, g);
        if (nodeI != 0) matrixBuilder.add(nodeJ - 1, nodeI - 1, -g);
    }
}

inline void stampCapacitorTrapezoidal(int nodeI, int nodeJ, double C, double h, 
                                       double v_prev, double i_prev, 
                                       MatrixConstructor& matrixBuilder, std::vector<double>& rhsVector)
{
    double G_eq = (2.0 * C) / h;
    double I_eq = -G_eq * v_prev - i_prev;

    if (nodeI != 0) {
        matrixBuilder.add(nodeI - 1, nodeI - 1, G_eq);
        if (nodeJ != 0) matrixBuilder.add(nodeI - 1, nodeJ - 1, -G_eq);
    }
    if (nodeJ != 0) {
        matrixBuilder.add(nodeJ - 1, nodeJ - 1, G_eq);
        if (nodeI != 0) matrixBuilder.add(nodeJ - 1, nodeI - 1, -G_eq);
    }

    if (nodeI != 0) rhsVector[nodeI - 1] -= I_eq;
    if (nodeJ != 0) rhsVector[nodeJ - 1] += I_eq;
}

inline void stampVoltageSourceAsNorton(int nodePos, int nodeNeg, double voltage, 
                                        MatrixConstructor& matrixBuilder, std::vector<double>& rhsVector)
{
    double r_int = 1e-6;
    double g_int = 1.0 / r_int;
    double i_eq = voltage * g_int;

    stampResistor(nodePos, nodeNeg, r_int, matrixBuilder);

    if (nodePos != 0) rhsVector[nodePos - 1] += i_eq;
    if (nodeNeg != 0) rhsVector[nodeNeg - 1] -= i_eq;
}

// ============================================================================
// CIRCUITSIM CLASS
// ============================================================================

struct SimulationResult {
    std::vector<double> nodeVoltages;
    double finalResidual;
    int iterations;
    bool converged;
};

class CircuitSim
{
public:
    CircuitSim() = default;
    
    SimulationResult solveDC(TensorNetlist& netlist);
    SimulationResult stepTransient(TensorNetlist& netlist, double timeStep);

private:
    MatrixConstructor matrixBuilder;
    std::vector<double> rhsVector;
};

// ============================================================================
// IMPLEMENTATION
// ============================================================================

inline SimulationResult CircuitSim::solveDC(TensorNetlist& netlist)
{
    // Ensure we have enough nodes in vectors
    if (netlist.numGlobalNodes == 0) return {{}, 0.0, 0, true};

    matrixBuilder = MatrixConstructor();
    rhsVector.assign(netlist.numGlobalNodes, 0.0);

    // 1. Stamp Global Components
    for (const auto& r : netlist.globalBlock.resistors) {
        stampResistor(r.nodePos, r.nodeNeg, r.resistance, matrixBuilder);
    }
    for (const auto& v : netlist.globalBlock.voltageSources) {
        stampVoltageSourceAsNorton(v.nodePos, v.nodeNeg, v.voltage, matrixBuilder, rhsVector);
    }

    // 2. Stamp Instances
    for (const auto& instance : netlist.instances) {
        auto it = std::find_if(netlist.blockDefinitions.begin(), netlist.blockDefinitions.end(), 
                               [&](const TensorBlock& b) { return b.name == instance.blockName; });
        if (it == netlist.blockDefinitions.end()) continue;

        const TensorBlock& block = *it;
        auto mapNode = [&](int localNode) {
            return (localNode == 0) ? 0 : instance.nodeMapping[localNode - 1];
        };

        for (const auto& r : block.resistors) {
            stampResistor(mapNode(r.nodePos), mapNode(r.nodeNeg), r.resistance, matrixBuilder);
        }
    }
    
    // Add small diagonal term to prevent singularity if nodes are floating
    // (This is a simple hack for robustness)
    for(int i=0; i<netlist.numGlobalNodes; ++i) {
        matrixBuilder.add(i, i, 1e-12);
    }

    Csr_matrix A = matrixBuilder.createCsr();
    
    // Safety check for empty matrix (single node circuit)
    if (A.rows == 0) return {{}, 0.0, 0, true};
    
    SolverResult solverRes = solvePCG(A, rhsVector);
    
    return {solverRes.solution, solverRes.finalResidual, solverRes.iterations, solverRes.converged};
}

inline SimulationResult CircuitSim::stepTransient(TensorNetlist& netlist, double timeStep)
{
    if (netlist.numGlobalNodes == 0) return {{}, 0.0, 0, true};

    matrixBuilder = MatrixConstructor();
    rhsVector.assign(netlist.numGlobalNodes, 0.0);

    // 1. Stamp Global Components
    for (const auto& r : netlist.globalBlock.resistors) {
        stampResistor(r.nodePos, r.nodeNeg, r.resistance, matrixBuilder);
    }
    for (const auto& c : netlist.globalBlock.capacitors) {
        stampCapacitorTrapezoidal(c.nodePos, c.nodeNeg, c.capacitance, timeStep, 
                                   c.v_prev, c.i_prev, matrixBuilder, rhsVector);
    }
    for (const auto& v : netlist.globalBlock.voltageSources) {
        stampVoltageSourceAsNorton(v.nodePos, v.nodeNeg, v.voltage, matrixBuilder, rhsVector);
    }

    // 2. Stamp Instances
    for (const auto& instance : netlist.instances) {
        auto it = std::find_if(netlist.blockDefinitions.begin(), netlist.blockDefinitions.end(), 
                               [&](const TensorBlock& b) { return b.name == instance.blockName; });
        if (it == netlist.blockDefinitions.end()) continue;

        const TensorBlock& block = *it;
        auto mapNode = [&](int localNode) {
            return (localNode == 0) ? 0 : instance.nodeMapping[localNode - 1];
        };

        for (const auto& r : block.resistors) {
            stampResistor(mapNode(r.nodePos), mapNode(r.nodeNeg), r.resistance, matrixBuilder);
        }
        // NOTE: Capacitors in instances are not stamped here in this simple version
        // because they need distinct history states for each instance!
        // This is Phase 1.5 work.
    }

    for(int i=0; i<netlist.numGlobalNodes; ++i) {
        matrixBuilder.add(i, i, 1e-12);
    }

    Csr_matrix A = matrixBuilder.createCsr();
    if (A.rows == 0) return {{}, 0.0, 0, true};
    
    SolverResult solverRes = solvePCG(A, rhsVector);

    // 3. Update Global History
    for (auto& c : netlist.globalBlock.capacitors) {
        double v_new = 0.0;
        if (c.nodePos != 0) v_new += solverRes.solution[c.nodePos - 1];
        if (c.nodeNeg != 0) v_new -= solverRes.solution[c.nodeNeg - 1];

        double G_eq = (2.0 * c.capacitance) / timeStep;
        double I_eq = -G_eq * c.v_prev - c.i_prev;
        
        c.i_prev = G_eq * v_new + I_eq;
        c.v_prev = v_new;
    }

    return {solverRes.solution, solverRes.finalResidual, solverRes.iterations, solverRes.converged};
}