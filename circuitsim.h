#pragma once
#include "linalg.h"
#include "circuit.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <cassert>  // NASA Rule #5

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
    if (nodeI != 0) 
    {
        matrixBuilder.add(nodeI - 1, nodeI - 1, g);
        if (nodeJ != 0) matrixBuilder.add(nodeI - 1, nodeJ - 1, -g);
    }
    if (nodeJ != 0) 
    {
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

inline void stampMosfet(int d, int g, int s, int b, double gm, double gds, MatrixConstructor& mat) {
    // Linearized Small-Signal Model Stamp
    // i_d += gm * (vg - vs) + gds * (vd - vs)
    // i_s -= gm * (vg - vs) - gds * (vd - vs)
    
    auto add = [&](int r, int c, double val) {
        if (r != 0 && c != 0) mat.add(r-1, c-1, val);
    };
    
    // GM Term (VCCS)
    add(d, g, gm); add(d, s, -gm);
    add(s, g, -gm); add(s, s, gm);
    
    // GDS Term (Resistor)
    add(d, d, gds); add(d, s, -gds);
    add(s, d, -gds); add(s, s, gds);
}

// ============================================================================
// CIRCUITSIM CLASS
// ============================================================================

#include "simtrace.h"
#include "tensorscheduler.h"

// ============================================================================
// CIRCUITSIM CLASS
// ============================================================================

/**
 * @brief CircuitSim - Physics and Solver Engine.
 * 
 * In Phase 0/1, this produces SimulationResult (legacy) or writes to a 
 * SimulationTrace (modern research-grade).
 */
class CircuitSim
{
    public:
        CircuitSim() = default;
        
        SimulationStep solveDC(TensorNetlist& netlist);
        SimulationStep stepTransient(TensorNetlist& netlist, double timeStep, double currentTime);

    private:
        MatrixConstructor matrixBuilder;
        std::vector<double> rhsVector;
        TensorScheduler scheduler;
        
        // --- SOLID/NASA: Extracted Helper Functions (SRP, Rule #4) ---
        
        /**
         * @brief Stamps global DC components (resistors, voltage sources).
         */
        void stampGlobalDC(const TensorBlock& block) {
            for (const auto& r : block.resistors) {
                stampResistor(r.nodeTerminal1, r.nodeTerminal2, r.resistance_ohms, matrixBuilder);
            }
            for (const auto& v : block.voltageSources) {
                stampVoltageSourceAsNorton(v.nodePositive, v.nodeNegative, v.voltage_V, matrixBuilder, rhsVector);
            }
        }
        
        /**
         * @brief Stamps global transient components (resistors, capacitors, voltage sources).
         */
        void stampGlobalTransient(TensorBlock& block, double timeStep) {
            for (const auto& r : block.resistors) {
                stampResistor(r.nodeTerminal1, r.nodeTerminal2, r.resistance_ohms, matrixBuilder);
            }
            for (auto& c : block.capacitors) {
                stampCapacitorTrapezoidal(c.nodePlate1, c.nodePlate2, c.capacitance_farads, timeStep, 
                                           c.voltage_prev_V, c.current_prev_A, matrixBuilder, rhsVector);
            }
            for (const auto& v : block.voltageSources) {
                stampVoltageSourceAsNorton(v.nodePositive, v.nodeNegative, v.voltage_V, matrixBuilder, rhsVector);
            }
        }
        
        /**
         * @brief Stamps all batched jobs from the scheduler.
         */
        void stampBatchedJobs(const TensorNetlist& netlist, bool includeTransient, double timeStep = 0.0) {
            for (const auto& job : scheduler.jobs) {
                const TensorBlock& block = *job.blockTmpl;
                
                for (int instanceIdx : job.instanceIds) {
                    const auto& instance = netlist.instances[instanceIdx];
                    
                    auto mapNode = [&](int localNode) {
                        return (localNode == 0) ? 0 : instance.nodeMapping[localNode - 1];
                    };

                    for (const auto& r : block.resistors) {
                        stampResistor(mapNode(r.nodeTerminal1), mapNode(r.nodeTerminal2), r.resistance_ohms, matrixBuilder);
                    }
                    
                    for (const auto& m : block.mosfets) {
                        double W = m.channelWidth_m;
                        double L = m.channelLength_m;
                        
                        std::string paramW = m.instanceName + ".W";
                        if (instance.parameters.count(paramW)) W = instance.parameters.at(paramW);
                        
                        std::string paramL = m.instanceName + ".L";
                        if (instance.parameters.count(paramL)) L = instance.parameters.at(paramL);
                        
                        double Kp = 200e-6;
                        double Vov = 0.5;
                        double gm = Kp * (W/L) * Vov;
                        double gds = gm / 100.0;
                        
                        stampMosfet(mapNode(m.nodeDrain), mapNode(m.nodeGate), mapNode(m.nodeSource), mapNode(m.nodeBody), gm, gds, matrixBuilder);
                    }
                }
            }
        }
        
        /**
         * @brief Adds small diagonal for matrix conditioning.
         */
        void addDiagonalConditioning(int n) {
            for (int i = 0; i < n; ++i) {
                matrixBuilder.add(i, i, 1e-12);
            }
        }
};

inline SimulationStep CircuitSim::solveDC(TensorNetlist& netlist)
{
    // NASA Rule #4: Function kept under 60 lines via helper extraction
    if (netlist.numGlobalNodes == 0) return {0.0, {}, {0, 0.0, true}};

    matrixBuilder = MatrixConstructor();
    rhsVector.assign(netlist.numGlobalNodes, 0.0);
    
    scheduler.schedule(netlist);
    stampGlobalDC(netlist.globalBlock);
    stampBatchedJobs(netlist, false);
    addDiagonalConditioning(netlist.numGlobalNodes);

    Csr_matrix A = matrixBuilder.createCsr();
    if (A.rows == 0) return {0.0, {}, {0, 0.0, true}};
    
    SolverResult solverRes = solvePCG(A, rhsVector);
    return {0.0, solverRes.solution, {solverRes.iterations, solverRes.finalResidual, solverRes.converged}};
}

inline SimulationStep CircuitSim::stepTransient(TensorNetlist& netlist, double timeStep, double currentTime)
{
    // NASA Rule #4: Function kept under 60 lines via helper extraction
    if (netlist.numGlobalNodes == 0) return {currentTime, {}, {0, 0.0, true}};

    matrixBuilder = MatrixConstructor();
    rhsVector.assign(netlist.numGlobalNodes, 0.0);
    
    scheduler.schedule(netlist);
    stampGlobalTransient(netlist.globalBlock, timeStep);
    stampBatchedJobs(netlist, true, timeStep);
    addDiagonalConditioning(netlist.numGlobalNodes);

    Csr_matrix A = matrixBuilder.createCsr();
    if (A.rows == 0) return {currentTime, {}, {0, 0.0, true}};
    
    SolverResult solverRes = solvePCG(A, rhsVector);

    // Update Global Capacitor History
    for (auto& c : netlist.globalBlock.capacitors) {
        double v_new = 0.0;
        if (c.nodePlate1 != 0) v_new += solverRes.solution[c.nodePlate1 - 1];
        if (c.nodePlate2 != 0) v_new -= solverRes.solution[c.nodePlate2 - 1];

        double G_eq = (2.0 * c.capacitance_farads) / timeStep;
        double I_eq = -G_eq * c.voltage_prev_V - c.current_prev_A;
        
        c.current_prev_A = G_eq * v_new + I_eq;
        c.voltage_prev_V = v_new;
    }

    return {currentTime, solverRes.solution, {solverRes.iterations, solverRes.finalResidual, solverRes.converged}};
}