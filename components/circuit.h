#pragma once
#include <vector>
#include <string>
#include <algorithm>
#include <map>
#include <cassert>  // NASA Rule #5
#include "graph_canonical.h"

/*
 * circuit.h
 * Circuit Netlist - Component definitions and hierarchical storage.
 *
 * Variable Naming Convention (EE Standard):
 *   - Resistor: nodeTerminal1/2
 *   - Capacitor: nodePlate1/2
 *   - Inductor: nodeCoil1/2
 *   - Diode: nodeAnode, nodeCathode
 *   - Mosfet: drain, gate, source, body
 *   - BJT: collector, base, emitter
 *   - Controlled Sources: nodeOutputPos/Neg, nodeControlPos/Neg
 */


// ============================================================================
// PASSIVE COMPONENTS
// ============================================================================

/*
 * Resistor (R)
 * Ohm's Law: V = I * R
 * MNA Stamp (Conductance Form):
 *   G[n1][n1] += 1/R, G[n2][n2] += 1/R
 *   G[n1][n2] -= 1/R, G[n2][n1] -= 1/R
 */
struct Resistor {
    int nodeTerminal1;            // First terminal (node index)
    int nodeTerminal2;            // Second terminal (node index)
    double resistance_ohms;       // Resistance in Ohms [Ω]
};

/*
 * Capacitor (C)
 * Physics: i_C = C * dv_C/dt
 * Trapezoidal Integration Model:
 *   G_eq = 2*C / dt
 *   I_eq = G_eq * v_prev + i_prev
 * The capacitor is replaced by a conductance in parallel with a history current source.
 */
struct Capacitor {
    int nodePlate1;               // Positive plate (node index)
    int nodePlate2;               // Negative plate (node index)
    double capacitance_farads;    // Capacitance in Farads [F]
    
    // Generic History Buffer (Index 0 = t_n-1, Index 1 = t_n-2, etc.)
    std::vector<double> voltage_history; 
    std::vector<double> current_history;
};

/*
 * Inductor (L)
 * Physics: v_L = L * di_L/dt
 * Backward Euler Integration Model:
 *   R_eq = L / dt
 *   V_eq = R_eq * i_prev
 * Inductor adds an auxiliary current variable to the MNA system.
 */
struct Inductor {
    int nodeCoil1;                // First coil terminal (node index)
    int nodeCoil2;                // Second coil terminal (node index)
    double inductance_henries;    // Inductance in Henries [H]
    
    // Generic History Buffer
    std::vector<double> current_history;
    std::vector<double> voltage_history;
};

/*
 * Mutual Inductor (K) - Transformer Coupling
 * Physics:
 *   v1 = L1 * di1/dt + M * di2/dt
 *   v2 = M * di1/dt + L2 * di2/dt
 * Where M = couplingCoefficient_k * sqrt(L1 * L2)
 */
struct MutualInductor {
    int inductor1_index;          // Index of first inductor in inductors vector
    int inductor2_index;          // Index of second inductor
    double couplingCoefficient_k; // Coupling factor (0.0 = none, 1.0 = ideal)
};

// ============================================================================
// INDEPENDENT SOURCES
// ============================================================================

/*
 * Independent Voltage Source (V)
 * Ideal: Forces v(+) - v(-) = V_dc
 * Adds an auxiliary branch current to MNA.
 */
struct VoltageSource {
    int nodePositive;             // Positive terminal (+)
    int nodeNegative;             // Negative terminal (-)
    double voltage_V;             // DC Voltage [V]
};

/*
 * Independent Current Source (I)
 * Ideal: Forces i = I_dc (from + to -)
 * Stamps directly into RHS vector.
 */
struct CurrentSource {
    int nodePositive;             // Current enters (+)
    int nodeNegative;             // Current exits (-)
    double current_A;             // DC Current [A]
};

// ============================================================================
// CONTROLLED SOURCES (Linear Dependent Sources)
// ============================================================================

/*
 * Voltage-Controlled Voltage Source (VCVS / E-element)
 * v_out = voltageGain * v_control
 * Used for ideal op-amp models.
 */
struct VoltageControl_Voltage_Source
{
    int nodeOutputPos, nodeOutputNeg;     // Output port
    int nodeControlPos, nodeControlNeg;   // Sensing port
    double voltageGain;                   // Av (dimensionless)
};

/*
 * Voltage-Controlled Current Source (VCCS / G-element)
 * i_out = transconductance_siemens * v_control
 * Used for MOSFET small-signal models (gm).
 */
struct VoltageControl_Current_Source 
{
    int nodeOutputPos, nodeOutputNeg;     // Output port (current flows + to -)
    int nodeControlPos, nodeControlNeg;   // Sensing port
    double transconductance_siemens;      // gm [S or A/V]
};

/*
 * Current-Controlled Voltage Source (CCVS / H-element)
 * v_out = transresistance_ohms * i_control
 * Requires sensing current through a zero-volt source.
 */
struct CurrentControl_Voltage_Source 
{
    int nodeOutputPos, nodeOutputNeg;
    int sensingVoltageSourceIndex;        // Index of V-source used to sense current
    double transresistance_ohms;          // rm [Ω or V/A]
};

/*
 * Current-Controlled Current Source (CCCS / F-element)
 * i_out = currentGain * i_control
 * Used for BJT current mirror models (Beta).
 */
struct CurrentControl_Current_Source 
{
    int nodeOutputPos, nodeOutputNeg;
    int sensingVoltageSourceIndex;
    double currentGain;                   // Beta (dimensionless)
};

// ============================================================================
// NON-LINEAR SEMICONDUCTOR DEVICES
// ============================================================================

/*
 * Diode (D) - PN Junction
 * Shockley Equation:
 *   I_D = I_S * (exp(V_D / (N * V_T)) - 1)
 * Where:
 *   I_S = Saturation current (typ. 1e-14 A)
 *   N   = Emission coefficient (1.0 for ideal, 1.5-2 for real)
 *   V_T = Thermal voltage (kT/q = 25.85 mV @ 300K)
 *
 * Newton-Raphson Linearization:
 *   g_eq = dI/dV = (I_S / (N*V_T)) * exp(V_k / (N*V_T))
 *   I_eq = I(V_k) - g_eq * V_k
 */
struct Diode {
    int anode;                        // P-type side (+)
    int cathode;                      // N-type side (-)
    double saturationCurrent_I_S_A;       // I_S [A]
    double emissionCoefficient_N;         // N (dimensionless)
    double thermalVoltage_V_T_V;          // V_T [V]
    
    // Thermal (Optional, default 300.15K)
    double temperature_K = 300.15;
};

// ...

struct Mosfet {
    std::string instanceName;             // e.g., "M1"
    int drain;                        // D terminal
    int gate;                         // G terminal
    int source;                       // S terminal
    int body;                         // B (Bulk/Substrate) terminal
    std::string modelName;                // Reference to .model card (e.g., "NMOS_SKY130")
    double channelWidth_m;                // W [meters]
    double channelLength_m;               // L [meters]
    
    double temperature_K = 300.15;
    
    // Operating Point (Updated each Newton iteration)
    double V_GS = 0.0, V_DS = 0.0, V_BS = 0.0;
};

// ...

struct BJT {
    std::string instanceName;
    int nodeCollector;                    // C terminal
    int base;                             // B terminal
    int emitter;                          // E terminal
    std::string modelName;
    bool isNPN;                           // true=NPN, false=PNP
    
    // Model Parameters (Per-Instance for Phase 1, ideally per-model lookup)
    double betaF;                         // Forward Beta
    double betaR;                         // Reverse Beta
    double saturationCurrent_I_S_A;       // Is [A]
    double thermalVoltage_V_T_V;          // Vt [V]
    
    double temperature_K = 300.15;
};

// ============================================================================
// INTERCONNECTS & TRANSMISSION LINES
// ============================================================================

/*
 * Lossless Transmission Line (T)
 * Telegrapher's Equations yield a time-delayed reflection model:
 *   v_1(t) + Z_0 * i_1(t) = v_2(t - T_d) + Z_0 * i_2(t - T_d)
 * Modeled as history-dependent current sources at each port.
 */
struct TransmissionLine {
    int nodePort1Pos, nodePort1Neg;       // Port 1 terminals
    int nodePort2Pos, nodePort2Neg;       // Port 2 terminals
    double characteristicImpedance_Z0_ohms; // Z_0 [Ω]
    double propagationDelay_Td_s;         // T_d [seconds]
};

/*
 * Probe (Measurement Tool)
 * Does not affect physics (infinite impedance).
 * Used to request V(pos) - V(neg) for plotting/GUI.
 */
struct Probe {
    std::string name;
    int nodePositive;
    int nodeNegative;
};

// ============================================================================
// TENSOR HIERARCHY (Hierarchical Netlist Structure)
// ============================================================================

/*
 * TensorBlock - A reusable sub-circuit definition (e.g., Inverter, SRAM cell).
 * Components use LOCAL node indices (0 = local ground, 1..N = internal nodes).
 */

struct TensorBlock {
    std::string name;
    int numInternalNodes = 0;
    
    // Passive Components
    std::vector<Resistor> resistors;
    std::vector<Capacitor> capacitors;
    std::vector<Inductor> inductors;
    std::vector<MutualInductor> mutualInductors;
    
    // Independent Sources
    std::vector<VoltageSource> voltageSources;
    std::vector<CurrentSource> currentSources;
    
    // Controlled Sources
    std::vector<VoltageControl_Voltage_Source> voltageControlledVoltageSources;
    std::vector<VoltageControl_Current_Source> voltageControlledCurrentSources;
    std::vector<CurrentControl_Voltage_Source> currentControlledVoltageSources;
    std::vector<CurrentControl_Current_Source> currentControlledCurrentSources;
    
    // Semiconductors
    std::vector<Diode> diodes;
    std::vector<Mosfet> mosfets;
    std::vector<BJT> bjts;
    
    // Interconnects
    // Interconnects
    std::vector<TransmissionLine> transmissionLines;
    
    // Aux
    std::vector<Probe> probes;

    bool isFrozen = false;       // Phase 1: Structural Immutability
    size_t topologyHash = 0;     // Phase 4: Symbolic Reuse Key

    // --- Helper Methods ---
    void freeze() {
        if (isFrozen) return;
        isFrozen = true;
        computeTopologyHash();
    }

    void computeTopologyHash() {
        // Phase 1.4: Canonical Graph Hashing
        // Uses orientation-invariant edges and BFS-based node relabeling.
        std::vector<GraphCanonical::CanonicalEdge> edges;
        
        // Type tags: 0=Resistor, 1=Capacitor, 2=Inductor, 3=Diode, 4=Mosfet, 5=BJT
        for (const auto& r : resistors) 
            edges.emplace_back(r.nodeTerminal1, r.nodeTerminal2, 0);
        for (const auto& c : capacitors) 
            edges.emplace_back(c.nodePlate1, c.nodePlate2, 1);
        for (const auto& l : inductors) 
            edges.emplace_back(l.nodeCoil1, l.nodeCoil2, 2);
        for (const auto& d : diodes) 
            edges.emplace_back(d.anode, d.cathode, 3);
        for (const auto& m : mosfets) {
            // MOSFET is a 4-terminal device; we represent as (D-S) and (G-S) edges
            edges.emplace_back(m.drain, m.source, 4);
            edges.emplace_back(m.gate, m.source, 4);
        }
        for (const auto& b : bjts) {
            // BJT: (C-E) and (B-E) edges
            edges.emplace_back(b.nodeCollector, b.emitter, 5);
            edges.emplace_back(b.base, b.emitter, 5);
        }
        
        // Ground is assumed to be node 0 for blocks
        topologyHash = GraphCanonical::computeTopologyHash(edges, 0);
    }

    void updateNodeCount(int node) {
        if (isFrozen) return; // Prevent node count updates on frozen blocks
        if (node > numInternalNodes) numInternalNodes = node;
    }

    void addResistor(int n1, int n2, double R) {
        assert(n1 >= 0 && n2 >= 0 && "Node indices must be not negative");
        assert(R > 0 && "Resistance must be positive");
        if (isFrozen) return;
        resistors.push_back({n1, n2, R});
        updateNodeCount(n1); updateNodeCount(n2);
    }
    void addCapacitor(int n1, int n2, double C) {
        assert(n1 >= 0 && n2 >= 0 && "Node indices must be not negative");
        assert(C > 0 && "Capacitance must be positive");
        if (isFrozen) return;
        Capacitor cap = {n1, n2, C};
        // Initialize history with 3 slots (enough for Gear 3)
        cap.voltage_history.assign(3, 0.0);
        cap.current_history.assign(3, 0.0);
        capacitors.push_back(cap);
        updateNodeCount(n1); updateNodeCount(n2);
    }
    void addInductor(int n1, int n2, double L) {
        assert(n1 >= 0 && n2 >= 0 && "Node indices must be not negative");
        assert(L > 0 && "Inductance must be positive");
        if (isFrozen) return;
        Inductor ind = {n1, n2, L};
        ind.current_history.assign(3, 0.0);
        ind.voltage_history.assign(3, 0.0);
        inductors.push_back(ind);
        updateNodeCount(n1); updateNodeCount(n2);
    }
    void addVoltageSource(int nPos, int nNeg, double V) {
        assert(nPos >= 0 && nNeg >= 0 && "Node indices must be not negative");
        if (isFrozen) return;
        voltageSources.push_back({nPos, nNeg, V});
        updateNodeCount(nPos); updateNodeCount(nNeg);
    }
    void addCurrentSource(int nPos, int nNeg, double I) {
        if (isFrozen) return;
        currentSources.push_back({nPos, nNeg, I});
        updateNodeCount(nPos); updateNodeCount(nNeg);
    }
    void addDiode(int anode, int cathode, double Is, double N = 1.0, double Vt = 0.02585) {
        if (isFrozen) return;
        diodes.push_back({anode, cathode, Is, N, Vt});
        updateNodeCount(anode); updateNodeCount(cathode);
    }
    void addMosfet(const std::string& name, int D, int G, int S, int B, 
                   const std::string& model, double W, double L) {
        if (isFrozen) return;
        mosfets.push_back({name, D, G, S, B, model, W, L});
        updateNodeCount(D); updateNodeCount(G); updateNodeCount(S); updateNodeCount(B);
    }
    void addBJT(const std::string& name, int C, int B, int E, 
                const std::string& model, bool isNPN = true,
                double betaF = 100.0, double betaR = 1.0, 
                double Is = 1e-14, double Vt = 0.02585) {
        if (isFrozen) return;
        bjts.push_back({name, C, B, E, model, isNPN, betaF, betaR, Is, Vt});
        updateNodeCount(C); updateNodeCount(B); updateNodeCount(E);
    }
    void addTransmissionLine(int p1p, int p1n, int p2p, int p2n, double Z0, double Td) {
        if (isFrozen) return;
        transmissionLines.push_back({p1p, p1n, p2p, p2n, Z0, Td});
        updateNodeCount(p1p); updateNodeCount(p1n); updateNodeCount(p2p); updateNodeCount(p2n);
    }
    void addProbe(const std::string& name, int nPos, int nNeg) {
        if (isFrozen) return;
        probes.push_back({name, nPos, nNeg});
        // NOTE: Probes do NOT call updateNodeCount because they are passive observers
        // and should not inflate the matrix dimensions.
    }
};

// BlockInstance - A placed instance of a TensorBlock in the global netlist.

struct BlockInstance {
    std::string blockName;                // Name of the TensorBlock template
    std::vector<int> nodeMapping;         // Maps local node index -> global node index
    std::map<std::string, double> parameters; // Instance-specific parameters (e.g. "W", "L")
};

// TensorNetlist - The top-level hierarchical circuit container.

class TensorNetlist {
public:
    std::map<std::string, TensorBlock> blockDefinitions; // Library of blocks
    TensorBlock globalBlock;                             // Top-level components
    std::vector<BlockInstance> instances;                // Placed block instances
    int numGlobalNodes = 0;

    TensorNetlist() { globalBlock.name = "GLOBAL"; }

    void defineBlock(TensorBlock block) {
        block.freeze();
        blockDefinitions[block.name] = block;
    }
    
    void addInstance(const std::string& blockName, const std::vector<int>& nodeMap) {
        instances.push_back({blockName, nodeMap});
        for (int n : nodeMap) if (n > numGlobalNodes) numGlobalNodes = n;
    }
    
    // --- Convenience Methods for Global Block ---
    void addResistor(int n1, int n2, double R) {
        globalBlock.addResistor(n1, n2, R);
        if (n1 > numGlobalNodes) numGlobalNodes = n1;
        if (n2 > numGlobalNodes) numGlobalNodes = n2;
    }
    void addCapacitor(int n1, int n2, double C) {
        globalBlock.addCapacitor(n1, n2, C);
        if (n1 > numGlobalNodes) numGlobalNodes = n1;
        if (n2 > numGlobalNodes) numGlobalNodes = n2;
    }
    void addInductor(int n1, int n2, double L) {
        globalBlock.addInductor(n1, n2, L);
        if (n1 > numGlobalNodes) numGlobalNodes = n1;
        if (n2 > numGlobalNodes) numGlobalNodes = n2;
    }
    void addVoltageSource(int nPos, int nNeg, double V) {
        globalBlock.addVoltageSource(nPos, nNeg, V);
        if (nPos > numGlobalNodes) numGlobalNodes = nPos;
        if (nNeg > numGlobalNodes) numGlobalNodes = nNeg;
    }
    void addCurrentSource(int nPos, int nNeg, double I) {
        globalBlock.addCurrentSource(nPos, nNeg, I);
        if (nPos > numGlobalNodes) numGlobalNodes = nPos;
        if (nNeg > numGlobalNodes) numGlobalNodes = nNeg;
    }
    void addDiode(int anode, int cathode, double Is) {
        globalBlock.addDiode(anode, cathode, Is);
        if (anode > numGlobalNodes) numGlobalNodes = anode;
        if (cathode > numGlobalNodes) numGlobalNodes = cathode;
    }
    void addMosfet(const std::string& name, int D, int G, int S, int B,
                   const std::string& model, double W, double L) {
        globalBlock.addMosfet(name, D, G, S, B, model, W, L);
        numGlobalNodes = std::max({numGlobalNodes, D, G, S, B});
    }
    void addBJT(const std::string& name, int C, int B, int E, 
                const std::string& model, bool isNPN = true,
                double betaF = 100.0, double betaR = 1.0, 
                double Is = 1e-14, double Vt = 0.02585) {
        globalBlock.addBJT(name, C, B, E, model, isNPN, betaF, betaR, Is, Vt);
        numGlobalNodes = std::max({numGlobalNodes, C, B, E});
    }
    void addTransmissionLine(int p1p, int p1n, int p2p, int p2n, double Z0, double Td) {
        globalBlock.addTransmissionLine(p1p, p1n, p2p, p2n, Z0, Td);
        numGlobalNodes = std::max({numGlobalNodes, p1p, p1n, p2p, p2n});
    }
    void addProbe(const std::string& name, int nPos, int nNeg) {
        globalBlock.addProbe(name, nPos, nNeg);
        // NOTE: Probes do NOT inflate numGlobalNodes because they are passive observers.
    }
};
