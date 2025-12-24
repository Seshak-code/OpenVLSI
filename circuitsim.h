#pragma once
#include "linalg.h"
#include <vector>
#include <stdexcept>
#include <cmath>

/**
 * @file circuitsim.h
 * @brief Modified Nodal Analysis (MNA) circuit simulator using the sparse linalg library.
 * 
 * ============================================================================
 * MODIFIED NODAL ANALYSIS (MNA) - THEORY
 * ============================================================================
 * 
 * MNA formulates a circuit as a matrix equation: [A][x] = [z]
 * 
 * The matrix A is partitioned as:
 *   | G   B |   | v |   | i |
 *   |       | * |   | = |   |
 *   | C   D |   | j |   | e |
 * 
 * Where:
 *   G = Conductance matrix (NxN) - from resistors, transconductances
 *   B = Voltage source incidence matrix (NxM) - links voltage sources to nodes
 *   C = Transpose of B (MxN)
 *   D = Zero matrix for ideal voltage sources (MxM)
 *   v = Node voltages (N unknowns)
 *   j = Branch currents through voltage sources (M unknowns)
 *   i = Current source contributions at nodes (N values)
 *   e = Voltage source values (M values)
 * 
 * N = number of nodes (excluding ground = node 0)
 * M = number of voltage sources (each adds a branch current unknown)
 * 
 * ============================================================================
 * COMPONENT STAMPS
 * ============================================================================
 * 
 * Each component "stamps" its contribution into the G matrix and z vector.
 * We use MatrixConstructor from linalg.h to accumulate these stamps.
 * 
 * ----------------------------
 * RESISTOR (between node i and node j, value R)
 * ----------------------------
 * Conductance g = 1/R
 * 
 * Stamp into G matrix:
 *   G[i][i] += g
 *   G[j][j] += g
 *   G[i][j] -= g
 *   G[j][i] -= g
 * 
 * Pseudo-code:
 *   void stampResistor(int nodeI, int nodeJ, double resistance) {
 *       double conductance = 1.0 / resistance;
 *       if (nodeI != 0) {  // Skip ground node
 *           matrixBuilder.add(nodeI-1, nodeI-1, conductance);
 *           if (nodeJ != 0) matrixBuilder.add(nodeI-1, nodeJ-1, -conductance);
 *       }
 *       if (nodeJ != 0) {
 *           matrixBuilder.add(nodeJ-1, nodeJ-1, conductance);
 *           if (nodeI != 0) matrixBuilder.add(nodeJ-1, nodeI-1, -conductance);
 *       }
 *   }
 * 
 * ============================================================================
 * MEMORY ELEMENTS (Capacitors & Inductors) - Companion Models
 * ============================================================================
 * 
 * Memory elements store energy and their behavior depends on past states.
 * For transient (time-domain) simulation, we use "companion models" that
 * convert differential equations into algebraic equations at each timestep.
 * 
 * The Trapezoidal Rule is the most common integration method because it is:
 * - Second-order accurate (error ~ h²)
 * - A-stable (won't blow up for stiff circuits)
 * 
 * ----------------------------
 * CAPACITOR COMPANION MODEL
 * ----------------------------
 * 
 * CONSTITUTIVE EQUATION:
 *   i(t) = C * dv/dt
 * 
 * TRAPEZOIDAL DISCRETIZATION:
 *   i(t_n) = C * [v(t_n) - v(t_{n-1})] / h + C * [v'(t_n) + v'(t_{n-1})] / 2
 * 
 * Simplifying with i = C * dv/dt:
 *   i_n = (2C/h) * v_n - (2C/h) * v_{n-1} - i_{n-1}
 *   i_n = G_eq * v_n + I_eq
 * 
 * Where:
 *   G_eq = 2C / h                          (Equivalent conductance)
 *   I_eq = -G_eq * v_{n-1} - i_{n-1}        (History current source)
 *        = -(2C/h) * v_{n-1} - i_{n-1}
 * 
 * EQUIVALENT CIRCUIT:
 *   The capacitor becomes a resistor (R = h/2C) in PARALLEL with a current source (I_eq).
 * 
 *       node_i ----[G_eq]----+---- node_j
 *                            |
 *                           (I_eq)  (current source, direction: i -> j)
 *                            |
 *                           GND (conceptual, not actual ground)
 * 
 * STAMP (into MNA system):
 *   1. Stamp G_eq like a resistor between node_i and node_j
 *   2. Stamp I_eq as a current source (adds to RHS vector)
 * 
 * Pseudo-code:
 *   void stampCapacitorTransient(int nodeI, int nodeJ, double C, double h, 
 *                                 double v_prev, double i_prev) {
 *       double G_eq = (2.0 * C) / h;
 *       double I_eq = -G_eq * v_prev - i_prev;
 *       
 *       // Stamp equivalent conductance
 *       if (nodeI != 0) {
 *           matrixBuilder.add(nodeI-1, nodeI-1, G_eq);
 *           if (nodeJ != 0) matrixBuilder.add(nodeI-1, nodeJ-1, -G_eq);
 *       }
 *       if (nodeJ != 0) {
 *           matrixBuilder.add(nodeJ-1, nodeJ-1, G_eq);
 *           if (nodeI != 0) matrixBuilder.add(nodeJ-1, nodeI-1, -G_eq);
 *       }
 *       
 *       // Stamp history current source (I_eq flows from nodeI to nodeJ)
 *       if (nodeI != 0) rhsVector[nodeI-1] -= I_eq;  // Current leaves nodeI
 *       if (nodeJ != 0) rhsVector[nodeJ-1] += I_eq;  // Current enters nodeJ
 *   }
 * 
 * AFTER SOLVING (update history for next timestep):
 *   v_new = V[nodeI] - V[nodeJ];
 *   i_new = G_eq * v_new + I_eq;
 * 
 * ----------------------------
 * INDUCTOR COMPANION MODEL
 * ----------------------------
 * 
 * CONSTITUTIVE EQUATION:
 *   v(t) = L * di/dt
 * 
 * TRAPEZOIDAL DISCRETIZATION:
 *   v_n = L * [i_n - i_{n-1}] / h + L * [i'_n + i'_{n-1}] / 2
 * 
 * Rearranging for current:
 *   i_n = (h/2L) * v_n + (h/2L) * v_{n-1} + i_{n-1}
 *   i_n = G_eq * v_n + I_eq
 * 
 * Where:
 *   G_eq = h / (2L)                         (Equivalent conductance)
 *   I_eq = G_eq * v_{n-1} + i_{n-1}          (History current source)
 *        = (h/2L) * v_{n-1} + i_{n-1}
 * 
 * EQUIVALENT CIRCUIT:
 *   The inductor becomes a resistor (R = 2L/h) in PARALLEL with a current source (I_eq).
 * 
 *       node_i ----[G_eq]----+---- node_j
 *                            |
 *                           (I_eq)  (current source)
 *                            |
 * 
 * STAMP (into MNA system):
 *   Same pattern as capacitor - resistor + current source.
 * 
 * Pseudo-code:
 *   void stampInductorTransient(int nodeI, int nodeJ, double L, double h,
 *                                double v_prev, double i_prev) {
 *       double G_eq = h / (2.0 * L);
 *       double I_eq = G_eq * v_prev + i_prev;
 *       
 *       // Stamp equivalent conductance
 *       if (nodeI != 0) {
 *           matrixBuilder.add(nodeI-1, nodeI-1, G_eq);
 *           if (nodeJ != 0) matrixBuilder.add(nodeI-1, nodeJ-1, -G_eq);
 *       }
 *       if (nodeJ != 0) {
 *           matrixBuilder.add(nodeJ-1, nodeJ-1, G_eq);
 *           if (nodeI != 0) matrixBuilder.add(nodeJ-1, nodeI-1, -G_eq);
 *       }
 *       
 *       // Stamp history current source (I_eq flows from nodeI to nodeJ)
 *       if (nodeI != 0) rhsVector[nodeI-1] -= I_eq;
 *       if (nodeJ != 0) rhsVector[nodeJ-1] += I_eq;
 *   }
 * 
 * AFTER SOLVING (update history for next timestep):
 *   v_new = V[nodeI] - V[nodeJ];
 *   i_new = G_eq * v_new + I_eq;
 * 
 * ----------------------------
 * TRANSIENT SIMULATION LOOP
 * ----------------------------
 * 
 * Pseudo-code for time-stepping:
 *   // Initialize
 *   double t = 0.0, h = 1e-9;  // timestep
 *   std::vector<CapacitorState> capStates;  // {v_prev, i_prev} for each cap
 *   std::vector<InductorState> indStates;   // {v_prev, i_prev} for each ind
 *   
 *   while (t < t_end) {
 *       matrixBuilder.clear();
 *       rhsVector.assign(numUnknowns, 0.0);
 *       
 *       // Stamp resistors (memoryless)
 *       for (auto& R : resistors) stampResistor(R);
 *       
 *       // Stamp memory elements with history
 *       for (int i = 0; i < capacitors.size(); i++) {
 *           stampCapacitorTransient(capacitors[i], h, capStates[i].v, capStates[i].i);
 *       }
 *       for (int i = 0; i < inductors.size(); i++) {
 *           stampInductorTransient(inductors[i], h, indStates[i].v, indStates[i].i);
 *       }
 *       
 *       // Stamp sources
 *       stampSources(t);
 *       
 *       // Solve
 *       auto A = matrixBuilder.createCsr();
 *       auto x = solvePCG(A, rhsVector);
 *       
 *       // Update history states
 *       for (int i = 0; i < capacitors.size(); i++) {
 *           double v_new = x[capacitors[i].nodeI-1] - x[capacitors[i].nodeJ-1];
 *           double G_eq = (2.0 * capacitors[i].C) / h;
 *           double I_eq = -G_eq * capStates[i].v - capStates[i].i;
 *           capStates[i].v = v_new;
 *           capStates[i].i = G_eq * v_new + I_eq;
 *       }
 *       // Similar for inductors...
 *       
 *       t += h;
 *   }
 * 

 * ----------------------------
 * VOLTAGE SOURCE (from node- to node+, value V, source index k)
 * ----------------------------
 * A voltage source introduces a new unknown: the current through it.
 * Matrix is expanded to include this current.
 * 
 * Stamp into MNA system:
 *   B[nodePos][k] = +1    (current enters positive node)
 *   B[nodeNeg][k] = -1    (current leaves negative node)
 *   C[k][nodePos] = +1    (KVL equation)
 *   C[k][nodeNeg] = -1
 *   e[k] = V              (voltage source value in RHS)
 * 
 * Pseudo-code:
 *   void stampVoltageSource(int nodePos, int nodeNeg, double voltage, int sourceIndex) {
 *       int currentRow = numNodes + sourceIndex;  // Row for this source in expanded matrix
 *       
 *       if (nodePos != 0) {
 *           matrixBuilder.add(nodePos-1, currentRow, 1.0);   // B matrix
 *           matrixBuilder.add(currentRow, nodePos-1, 1.0);   // C matrix
 *       }
 *       if (nodeNeg != 0) {
 *           matrixBuilder.add(nodeNeg-1, currentRow, -1.0);  // B matrix
 *           matrixBuilder.add(currentRow, nodeNeg-1, -1.0);  // C matrix
 *       }
 *       rhsVector[currentRow] = voltage;  // e vector
 *   }
 * 
 * ----------------------------
 * CURRENT SOURCE (from node- to node+, value I)
 * ----------------------------
 * Current sources only affect the RHS vector, not the matrix.
 * 
 * Stamp:
 *   i[nodePos] -= I   (current leaves positive node)
 *   i[nodeNeg] += I   (current enters negative node)
 * 
 * Pseudo-code:
 *   void stampCurrentSource(int nodePos, int nodeNeg, double current) {
 *       if (nodePos != 0) rhsVector[nodePos-1] -= current;
 *       if (nodeNeg != 0) rhsVector[nodeNeg-1] += current;
 *   }
 * 
 * ----------------------------
 * GROUND NODE
 * ----------------------------
 * Node 0 is always ground (reference, V=0). It is NOT included in the matrix.
 * All stamps skip operations involving node 0.
 * When stamping, use (node - 1) as the matrix index for nodes > 0.
 * 
 * ============================================================================
 * SOLVING THE SYSTEM
 * ============================================================================
 * 
 * After all components are stamped:
 *   1. Build CSR matrix: Csr_matrix A = matrixBuilder.createCsr();
 *   2. Solve: std::vector<double> solution = solvePCG(A, rhsVector);
 *   3. solution[0..numNodes-1] = node voltages
 *   4. solution[numNodes..end] = branch currents through voltage sources
 * 
 */

// ----------------------------
// RESISTOR STAMP
// ----------------------------
void stampResistor(int nodeI, int nodeJ, double resistance, MatrixConstructor& matrixBuilder)
{
    double conductance = 1.0 / resistance;
    if (nodeI != 0) {
        matrixBuilder.add(nodeI - 1, nodeI - 1, conductance);
        if (nodeJ != 0) matrixBuilder.add(nodeI - 1, nodeJ - 1, -conductance);
    }
    if (nodeJ != 0) {
        matrixBuilder.add(nodeJ - 1, nodeJ - 1, conductance);
        if (nodeI != 0) matrixBuilder.add(nodeJ - 1, nodeI - 1, -conductance);
    }
}

/**
 * @brief CAPACITOR STAMP (Elastance Formulation)
 * 
 * FORMULA: 
 *   Elastance (S) = 1/C
 *   Equivalent Conductance (G_eq) = 2.0 / (S * h)
 */
void stampCapacitor(int nodeI, int nodeJ, double capacitance, double timeStep, 
                    double voltagePrev, double currentPrev, 
                    MatrixConstructor& matrixBuilder, std::vector<double>& rhsVector)
{
    // Elastance formulation as requested
    double elastance = 1.0 / capacitance;
    double equivalentAdmittance = 2.0 / (elastance * timeStep); // G_eq = 2C/h
    
    // History current source (Trapezoidal)
    double historyCurrent = -equivalentAdmittance * voltagePrev - currentPrev;

    // Stamp G_eq (Same pattern as resistor)
    if (nodeI != 0) {
        matrixBuilder.add(nodeI - 1, nodeI - 1, equivalentAdmittance);
        if (nodeJ != 0) matrixBuilder.add(nodeI - 1, nodeJ - 1, -equivalentAdmittance);
    }
    if (nodeJ != 0) {
        matrixBuilder.add(nodeJ - 1, nodeJ - 1, equivalentAdmittance);
        if (nodeI != 0) matrixBuilder.add(nodeJ - 1, nodeI - 1, -equivalentAdmittance);
    }

    // Update RHS with history current
    if (nodeI != 0) rhsVector[nodeI - 1] -= historyCurrent;
    if (nodeJ != 0) rhsVector[nodeJ - 1] += historyCurrent;
}

/**
 * @brief INDUCTOR STAMP (Susceptance/Inverse Inductance Formulation)
 * 
 * NOTE: While Susceptance (B) is traditionally an AC term (B = -1/wL),
 * in transient analysis we can use Inverse Inductance (Gamma = 1/L).
 * 
 * FORMULA:
 *   Inverse Inductance (Gamma) = 1/L
 *   Equivalent Conductance (G_eq) = (h * Gamma) / 2.0
 */
void stampInductor(int nodeI, int nodeJ, double inductance, double timeStep,
                   double voltagePrev, double currentPrev,
                   MatrixConstructor& matrixBuilder, std::vector<double>& rhsVector)
{
    // Inverse Inductance (Gamma)
    double gamma = 1.0 / inductance;
    double equivalentAdmittance = (timeStep * gamma) / 2.0; // G_eq = h / 2L
    
    // History current source (Trapezoidal)
    double historyCurrent = equivalentAdmittance * voltagePrev + currentPrev;

    // Stamp G_eq
    if (nodeI != 0) {
        matrixBuilder.add(nodeI - 1, nodeI - 1, equivalentAdmittance);
        if (nodeJ != 0) matrixBuilder.add(nodeI - 1, nodeJ - 1, -equivalentAdmittance);
    }
    if (nodeJ != 0) {
        matrixBuilder.add(nodeJ - 1, nodeJ - 1, equivalentAdmittance);
        if (nodeI != 0) matrixBuilder.add(nodeJ - 1, nodeI - 1, -equivalentAdmittance);
    }

    // Update RHS with history current
    if (nodeI != 0) rhsVector[nodeI - 1] -= historyCurrent;
    if (nodeJ != 0) rhsVector[nodeJ - 1] += historyCurrent;
}

// ----------------------------
// VOLTAGE SOURCE STAMP
// ----------------------------
void stampVoltageSource(int nodePos, int nodeNeg, double voltage, int sourceIndex, 
                        int numNodes, MatrixConstructor& matrixBuilder, std::vector<double>& rhsVector)
{
    int currentRow = numNodes + sourceIndex; // Sources are appended after node equations
    
    // Stamp B and C submatrices (1s and -1s)
    if (nodePos != 0) {
        matrixBuilder.add(nodePos - 1, currentRow, 1.0);
        matrixBuilder.add(currentRow, nodePos - 1, 1.0);
    }
    if (nodeNeg != 0) {
        matrixBuilder.add(nodeNeg - 1, currentRow, -1.0);
        matrixBuilder.add(currentRow, nodeNeg - 1, -1.0);
    }
    
    // Stamp RHS (Voltage value)
    rhsVector[currentRow] = voltage;
}

// ----------------------------
// CURRENT SOURCE STAMP
// ----------------------------
void stampCurrentSource(int nodePos, int nodeNeg, double current, std::vector<double>& rhsVector)
{
    if (nodePos != 0) rhsVector[nodePos - 1] -= current;
    if (nodeNeg != 0) rhsVector[nodeNeg - 1] += current;
}

class CircuitSim
{
private:
    // Component storage with State for Transient Analysis
    struct Resistor { int nodePos, nodeNeg; double resistance; };
    struct Capacitor { int nodePos, nodeNeg; double capacitance; double v_prev = 0; double i_prev = 0; };
    struct Inductor { int nodePos, nodeNeg; double inductance; double v_prev = 0; double i_prev = 0; };
    struct VoltageSource { int nodePos, nodeNeg; double voltage; int index; };
    struct CurrentSource { int nodePos, nodeNeg; double current; };
    
    std::vector<Resistor> resistors;
    std::vector<Capacitor> capacitors;
    std::vector<Inductor> inductors;
    std::vector<VoltageSource> voltageSources;
    std::vector<CurrentSource> currentSources;

    int numNodes = 0;
    // numVoltageSources is no longer needed for matrix sizing in the Norton approach
    
    MatrixConstructor matrixBuilder;
    std::vector<double> rhsVector;

    void updateNodeCount(int node) {
        if (node > numNodes) numNodes = node;
    }

    // Helper to stamp a voltage source as a Norton Equivalent (I || G)
    // This keeps the matrix SPD for the PCG solver.
    void stampVoltageSource(int nodePos, int nodeNeg, double voltage, MatrixConstructor& matrixBuilder, std::vector<double>& rhsVector) {
        // Use a small series resistance (e.g. 1 micro-Ohm) to model the source
        // V in series with R  <==>  I = V/R in parallel with G = 1/R
        double r_internal = 1e-6; 
        double g_internal = 1.0 / r_internal;
        double i_equivalent = voltage * g_internal;

        // Stamp G_internal (Resistor)
        stampResistor(nodePos, nodeNeg, r_internal, matrixBuilder);

        // Stamp I_equivalent (Current Source pointing from Neg to Pos internal)
        // Ideally Vsource drives Current OUT of Pos node.
        // Norton: Current Source I points from Neg to Pos.
        // Node Equation: I_entering = ...
        // So we add I to PosNode RHS, subtract from NegNode RHS.
        // Wait, standard convention: I_src flows from Neg to Pos inside the source.
        stampCurrentSource(nodePos, nodeNeg, -i_equivalent, rhsVector);
    }

public:
    CircuitSim() = default;
    ~CircuitSim() = default;

    void addResistor(int nodePositive, int nodeNegative, double resistance) {
        resistors.push_back({nodePositive, nodeNegative, resistance});
        updateNodeCount(nodePositive);
        updateNodeCount(nodeNegative);
    }

    void addCapacitor(int nodePositive, int nodeNegative, double capacitance) {
        capacitors.push_back({nodePositive, nodeNegative, capacitance});
        updateNodeCount(nodePositive);
        updateNodeCount(nodeNegative);
    }

    void addInductor(int nodePositive, int nodeNegative, double inductance) {
        inductors.push_back({nodePositive, nodeNegative, inductance});
        updateNodeCount(nodePositive);
        updateNodeCount(nodeNegative);
    }

    void addVoltageSource(int nodePositive, int nodeNegative, double voltage) {
        voltageSources.push_back({nodePositive, nodeNegative, voltage, 0});
        updateNodeCount(nodePositive);
        updateNodeCount(nodeNegative);
    }

    void addCurrentSource(int nodePositive, int nodeNegative, double current) {
        currentSources.push_back({nodePositive, nodeNegative, current});
        updateNodeCount(nodePositive);
        updateNodeCount(nodeNegative);
    }

    /**
     * @brief Runs a DC Operating Point or single transient step solve.
     *        For this simplified version, it assumes DC-only if no timeStep is provided?
     *        Actually, let's keep it structurally simple.
     */
    std::vector<double> solveDC() {
        // Clear previous build
        matrixBuilder = MatrixConstructor(); 
        
        // System size is just numNodes now (no extra rows for voltage sources)
        rhsVector.assign(numNodes, 0.0);

        // Stamp Components
        for (const auto& r : resistors) stampResistor(r.nodePos, r.nodeNeg, r.resistance, matrixBuilder);

        // Stamp Sources (using Norton Equivalent for Voltage Sources)
        for (const auto& v : voltageSources) {
            stampVoltageSource(v.nodePos, v.nodeNeg, v.voltage, matrixBuilder, rhsVector);
        }
        for (const auto& i : currentSources) {
            stampCurrentSource(i.nodePos, i.nodeNeg, i.current, rhsVector);
        }

        // Stamp Inductors as small resistors for DC
        for (const auto& ind : inductors) {
            stampResistor(ind.nodePos, ind.nodeNeg, 1e-9, matrixBuilder); 
        }

        Csr_matrix A = matrixBuilder.createCsr();
        return solvePCG(A, rhsVector);
    }

    /**
     * @brief Steps the transient simulation forward by one timeStep.
     */
    std::vector<double> stepTransient(double timeStep) {
        // Clear previous build
        matrixBuilder = MatrixConstructor();
        rhsVector.assign(numNodes, 0.0);

        // 1. Stamp Resistors
        for (const auto& r : resistors) stampResistor(r.nodePos, r.nodeNeg, r.resistance, matrixBuilder);

        // 2. Stamp Dynamic Components (using history)
        for (const auto& c : capacitors) {
            stampCapacitor(c.nodePos, c.nodeNeg, c.capacitance, timeStep, c.v_prev, c.i_prev, matrixBuilder, rhsVector);
        }
        for (const auto& ind : inductors) {
            stampInductor(ind.nodePos, ind.nodeNeg, ind.inductance, timeStep, ind.v_prev, ind.i_prev, matrixBuilder, rhsVector);
        }

        // 3. Stamp Sources
        for (const auto& v : voltageSources) {
            // Use Norton Equivalent to keep matrix SPD
            stampVoltageSource(v.nodePos, v.nodeNeg, v.voltage, matrixBuilder, rhsVector);
        }
        // Note: For time-varying sources, 'voltage' would be a function of time. 
        // Here we assume constant DC sources for step response.
        for (const auto& i : currentSources) {
            stampCurrentSource(i.nodePos, i.nodeNeg, i.current, rhsVector);
        }

        // 4. Solve
        Csr_matrix A = matrixBuilder.createCsr();
        std::vector<double> solution = solvePCG(A, rhsVector);

        // 5. Update History
        for (auto& c : capacitors) {
            double v_nodeI = (c.nodePos == 0) ? 0.0 : solution[c.nodePos - 1];
            double v_nodeJ = (c.nodeNeg == 0) ? 0.0 : solution[c.nodeNeg - 1];
            
            double v_new = v_nodeI - v_nodeJ;
            
            // Re-calculate derived current for next step
            // i_new = G_eq * v_new + I_eq
            // G_eq = 2C/h, I_eq = -(2C/h)*v_prev - i_prev
            double elastance = 1.0 / c.capacitance;
            double G_eq = 2.0 / (elastance * timeStep);
            // I_eq from previous step is -(G_eq_prev * v_prev_prev) - i_prev_prev
            // The current i_prev stored is the current *through* the capacitor at the *end* of the previous step.
            // So, I_eq for *this* step (using v_prev, i_prev) is: -G_eq * c.v_prev - c.i_prev
            // And the new current i_new is: G_eq * v_new + I_eq
            
            c.i_prev = G_eq * v_new + (-G_eq * c.v_prev - c.i_prev);
            c.v_prev = v_new; 
        }

        for (auto& ind : inductors) {
            double v_nodeI = (ind.nodePos == 0) ? 0.0 : solution[ind.nodePos - 1];
            double v_nodeJ = (ind.nodeNeg == 0) ? 0.0 : solution[ind.nodeNeg - 1];
            double v_new = v_nodeI - v_nodeJ;
            
            double gamma = 1.0 / ind.inductance;
            double G_eq = (timeStep * gamma) / 2.0;
            // I_eq from previous step is G_eq_prev * v_prev_prev + i_prev_prev
            // The current i_prev stored is the current *through* the inductor at the *end* of the previous step.
            // So, I_eq for *this* step (using v_prev, i_prev) is: G_eq * ind.v_prev + ind.i_prev
            // And the new current i_new is: G_eq * v_new + I_eq
            
            ind.i_prev = G_eq * v_new + (G_eq * ind.v_prev + ind.i_prev);
            ind.v_prev = v_new;
        }

        return solution;
    }
};