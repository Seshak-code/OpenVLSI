#pragma once
#include <vector>
#include <string>
#include <algorithm>
#include <map>
#include <set>
#include <cassert>
#include <iostream>
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
    std::string name;             // e.g. "R1"
    int nodeTerminal1;            // First terminal (node index)
    int nodeTerminal2;            // Second terminal (node index)
    double resistance_ohms;       // Resistance in Ohms [Ω]
    double tolerance_percent = 0.0; // Monte Carlo Tolerance (e.g. 1.0 = 1%)
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
    std::string name;             // e.g. "C1"
    int nodePlate1;               // Positive plate (node index)
    int nodePlate2;               // Negative plate (node index)
    double capacitance_farads;    // Capacitance in Farads [F]
    
    // Generic History Buffer (Index 0 = t_n-1, Index 1 = t_n-2, etc.)
    std::vector<double> voltage_history; 
    std::vector<double> current_history;
    
    double tolerance_percent = 0.0;
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
    std::string name;             // e.g. "L1"
    int nodeCoil1;                // First coil terminal (node index)
    int nodeCoil2;                // Second coil terminal (node index)
    double inductance_henries;    // Inductance in Henries [H]
    
    // Generic History Buffer
    std::vector<double> current_history;
    std::vector<double> voltage_history;
    
    double tolerance_percent = 0.0;
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
    std::string type = "";        // "RailDriver", "Generic", etc.
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
// DEPENDENT SOURCES (Bench + Block internal modes)
// ============================================================================

/*
 * VCVS - Voltage-Controlled Voltage Source
 * Vout = gain * Vcontrol
 * MNA: Adds branch equation with controlled coefficient.
 */
struct VCVS {
    int nodeOutPos, nodeOutNeg;   // Output terminals
    int nodeCtrlPos, nodeCtrlNeg; // Control terminals
    double gain;                  // Voltage gain (dimensionless)
    std::string role;             // For AI/debug: "bias", "feedback", etc.
};

/*
 * CCVS - Current-Controlled Voltage Source
 * Vout = transresistance * Icontrol
 * Requires a controlling branch (voltage source index).
 */
struct CCVS {
    int nodeOutPos, nodeOutNeg;
    int ctrlBranchIdx;            // Index of controlling branch current
    double transresistance;       // Ohms
    std::string role;
};

/*
 * VCCS - Voltage-Controlled Current Source
 * Iout = transconductance * Vcontrol
 * Stamps directly into conductance matrix.
 */
struct VCCS {
    int nodeOutPos, nodeOutNeg;
    int nodeCtrlPos, nodeCtrlNeg;
    double transconductance;      // Siemens
    std::string role;
};

/*
 * CCCS - Current-Controlled Current Source
 * Iout = currentGain * Icontrol
 */
struct CCCS {
    int nodeOutPos, nodeOutNeg;
    int ctrlBranchIdx;
    double currentGain;           // Dimensionless
    std::string role;
};

// ============================================================================
// SYSTEM SIMULATION SOURCES (Phase 2.9)
// ============================================================================

/*
 * Power Rail (Vdd/Vcc)
 * Models a realistic power supply network (PDN).
 * Model: V_ideal -> [ESL] -> [ESR] -> Node_Chip -> [C_decap] -> GND
 */
struct PowerRail {
    std::string name;              // "Vdd", "Vcc", "Vio"
    int nodeRail;                  // The chip-side node index
    double nominal_V;              // 1.8V, 3.3V, 5V, 12V
    double ripple_Vpp;             // Peak-to-peak ripple voltage
    double frequency_Hz;           // Ripple frequency (e.g., 1MHz switching)
    double ESR_ohms;               // Equivalent Series Resistance (package + regulator)
    double ESL_henries;            // Equivalent Series Inductance (bond wires)
    double capacitance_F;          // Decoupling capacitance (local)
    
    // Monte Carlo parameters
    double tolerance_percent;      // ±5% for typical regulators
    double temp_coefficient;       // Voltage drift with temperature [V/K]
    
    // Transient History (for L and C)
    std::vector<double> voltage_history;
    std::vector<double> current_history;
};

/*
 * Signal Source (Waveform Generator)
 * Models complex signal inputs (Clock, Data, Noise).
 */
struct SignalSource {
    std::string name;              // "CLK", "DATA_IN"
    int nodePositive;
    int nodeNegative;
    
    enum WaveformType { DC, PULSE, SINE, PWL, NOISE } type;
    
    double amplitude_V;
    double frequency_Hz;
    double phase_deg;
    double offset_V;
    double duty_cycle;             // 0.0 to 1.0 (Pulse only)
    double rise_time_s;            // (Pulse only)
    double fall_time_s;            // (Pulse only)
    
    // For future PWL
    std::vector<std::pair<double, double>> points;
};

// ============================================================================
// DIGITAL VERIFICATION COMPONENTS (Phase 13)
// ============================================================================

enum class LogicState {
    LOW = 0,
    HIGH = 1,
    UNKNOWN = 2, // X
    IMPEDANCE = 3 // Z
};

struct BusProbe {
    std::string name;             // e.g., "DATA_BUS"
    int bit_width;                // 8, 16, 32
    std::vector<int> nodeIndices; // [LSB ... MSB]
    
    // Logic Level Config
    double v_th_low = 0.8;        // Below 0.8V = 0
    double v_th_high = 2.0;       // Above 2.0V = 1
    
    // History
    struct DigitalSample {
        double time;
        uint64_t value;           // For up to 64-bit bus
        std::vector<LogicState> bitStates; // Detailed X/Z state
    };
    std::vector<DigitalSample> history;
};

/*
 * Simulation Environment
 * Global context for the simulation run.
 */
struct SimulationEnvironment {
    double ambient_temp_K = 300.0;    // 27°C
    double temp_ramp_K_per_s = 0.0;   // For thermal cycling
    
    // Process Corners (Monte Carlo)
    enum Corner { TT, FF, SS, SF, FS } process = TT;
    double vdd_variation_sigma = 0.0; // σ for MC
    double global_voltage_scale = 1.0; // PVT Voltage Scale
    int monte_carlo_seed = 0;          // Deterministic seed for MC runs
    
    // Noise Profile
    bool thermal_noise_enabled = false;
    bool flicker_noise_enabled = false;
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
    int nodeCollector = -1;                    // C terminal
    int base = -1;                             // B terminal
    int emitter = -1;                          // E terminal
    std::string modelName = "default";
    bool isNPN = true;                           // true=NPN, false=PNP
    
    // Model Parameters (Per-Instance for Phase 1, ideally per-model lookup)
    double betaF = 100.0;                         // Forward Beta
    double betaR = 1.0;                         // Reverse Beta
    double saturationCurrent_I_S_A = 1e-14;       // Is [A]
    double thermalVoltage_V_T_V = 0.02585;          // Vt [V]
    
    double temperature_K = 300.15;
};

/*
 * JFET (J) - Junction Field Effect Transistor
 * Shichman-Hodges Model:
 *   Cutoff:     Vgs <= Vto (for N-ch) or Vgs >= -Vto (for P-ch)
 *   Linear:     Id = Beta * (2*(Vgs-Vto)*Vds - Vds^2) * (1 + lambda*Vds)
 *   Saturation: Id = Beta * (Vgs - Vto)^2 * (1 + lambda*Vds)
 * Where:
 *   Beta = IDSS / Vto^2 (typ. 1e-4 A/V^2)
 *   Vto  = Pinch-off voltage (typ. -2V for N-ch, +2V for P-ch)
 *   lambda = Channel-length modulation (typ. 0.01)
 */
struct JFET {
    std::string instanceName;             // e.g., "J1"
    int drain = -1;                       // D terminal
    int gate = -1;                        // G terminal
    int source = -1;                      // S terminal
    std::string modelName = "default";
    bool isNChannel = true;               // true=N-ch, false=P-ch
    
    // Model Parameters
    double beta = 1e-4;                   // Transconductance parameter [A/V^2]
    double Vto = -2.0;                    // Pinch-off voltage [V] (negative for N-ch)
    double lambda = 0.01;                 // Channel-length modulation [1/V]
    double thermalVoltage_V_T_V = 0.02585; // For gate-source diode (optional)
    
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

    // System Simulation Sources (Phase 2.9)
    std::vector<PowerRail> powerRails;
    std::vector<SignalSource> signalSources;
    
    // Controlled Sources
    std::vector<VoltageControl_Voltage_Source> voltageControlledVoltageSources;
    std::vector<VoltageControl_Current_Source> voltageControlledCurrentSources;
    std::vector<CurrentControl_Voltage_Source> currentControlledVoltageSources;
    std::vector<CurrentControl_Current_Source> currentControlledCurrentSources;
    
    // Semiconductors
    std::vector<Diode> diodes;
    std::vector<Mosfet> mosfets;
    std::vector<BJT> bjts;
    std::vector<JFET> jfets;
    
    // Interconnects
    // Interconnects
    std::vector<TransmissionLine> transmissionLines;
    
    // Aux
    std::vector<Probe> probes;
    std::vector<BusProbe> busProbes;

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

    void addResistor(const std::string& name, int n1, int n2, double R) {
        assert(n1 >= 0 && n2 >= 0 && "Node indices must be not negative");
        assert(R > 0 && "Resistance must be positive");
        if (isFrozen) return;
        resistors.push_back({name, n1, n2, R});
        updateNodeCount(n1); updateNodeCount(n2);
    }
    // Backward compatibility overload (generates name if missing, though typically netlist parser provides it)
    void addResistor(int n1, int n2, double R) {
        static int r_count = 0;
        addResistor("R_AUTO_" + std::to_string(++r_count), n1, n2, R);
    }
    void addCapacitor(const std::string& name, int n1, int n2, double C) {
        assert(n1 >= 0 && n2 >= 0 && "Node indices must be not negative");
        assert(C > 0 && "Capacitance must be positive");
        if (isFrozen) return;
        Capacitor cap = {name, n1, n2, C};
        // Initialize history with 3 slots (enough for Gear 3)
        cap.voltage_history.assign(3, 0.0);
        cap.current_history.assign(3, 0.0);
        capacitors.push_back(cap);
        updateNodeCount(n1); updateNodeCount(n2);
    }
    void addCapacitor(int n1, int n2, double C) {
        static int c_count = 0;
        addCapacitor("C_AUTO_" + std::to_string(++c_count), n1, n2, C);
    }
    void addInductor(const std::string& name, int n1, int n2, double L) {
        assert(n1 >= 0 && n2 >= 0 && "Node indices must be not negative");
        assert(L > 0 && "Inductance must be positive");
        if (isFrozen) return;
        Inductor ind = {name, n1, n2, L};
        ind.current_history.assign(3, 0.0);
        ind.voltage_history.assign(3, 0.0);
        inductors.push_back(ind);
        updateNodeCount(n1); updateNodeCount(n2);
    }
     void addInductor(int n1, int n2, double L) {
        static int l_count = 0;
        addInductor("L_AUTO_" + std::to_string(++l_count), n1, n2, L);
    }
    void addVoltageSource(int nPos, int nNeg, double V, std::string type = "") {
        assert(nPos >= 0 && nNeg >= 0 && "Node indices must be not negative");
        if (isFrozen) return;
        voltageSources.push_back({nPos, nNeg, V, type});
        updateNodeCount(nPos); updateNodeCount(nNeg);
    }
    void addCurrentSource(int nPos, int nNeg, double I) {
        if (isFrozen) return;
        currentSources.push_back({nPos, nNeg, I});
        updateNodeCount(nPos); updateNodeCount(nNeg);
    }
    
    // System Simulation Helpers
    void addPowerRail(const std::string& name, int node, double V, double ripple = 0.0) {
        if (isFrozen) return;
        PowerRail rail;
        rail.name = name;
        rail.nodeRail = node;
        rail.nominal_V = V;
        rail.ripple_Vpp = ripple;
        // Defaults for simple model
        rail.frequency_Hz = 1e6;
        rail.ESR_ohms = 0.01;
        rail.ESL_henries = 1e-9;
        rail.capacitance_F = 100e-9;
        rail.tolerance_percent = 5.0;
        rail.temp_coefficient = 0.0;
        
        rail.voltage_history.assign(3, 0.0);
        rail.current_history.assign(3, 0.0);
        
        powerRails.push_back(rail);
        updateNodeCount(node);
    }
    
    void addSignalSource(const std::string& name, int nPos, int nNeg, 
                         SignalSource::WaveformType type, double amp, double freq) {
        if (isFrozen) return;
        SignalSource src;
        src.name = name;
        src.nodePositive = nPos;
        src.nodeNegative = nNeg;
        src.type = type;
        src.amplitude_V = amp;
        src.frequency_Hz = freq;
        // Defaults
        src.phase_deg = 0.0;
        src.offset_V = 0.0;
        src.duty_cycle = 0.5;
        
        signalSources.push_back(src);
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
        
        BJT newBjt;
        newBjt.instanceName = name;
        newBjt.nodeCollector = C;
        newBjt.base = B;
        newBjt.emitter = E;
        newBjt.modelName = model;
        newBjt.isNPN = isNPN;
        newBjt.betaF = betaF;
        newBjt.betaR = betaR;
        newBjt.saturationCurrent_I_S_A = Is;
        newBjt.thermalVoltage_V_T_V = Vt;
        
        // Defensive Fix: Prevent invalid Is
        if (newBjt.saturationCurrent_I_S_A <= 1e-20) {
             std::cerr << "[FIX] BJT " << name << " initialized with Is=" << newBjt.saturationCurrent_I_S_A 
                      << ". Forcing default 1e-14." << std::endl;
            newBjt.saturationCurrent_I_S_A = 1e-14;
        }

        bjts.push_back(newBjt);
        updateNodeCount(C); updateNodeCount(B); updateNodeCount(E);
    }
    void addJFET(const std::string& name, int D, int G, int S, 
                 const std::string& model, bool isNChannel = true,
                 double beta = 1e-4, double Vto = -2.0, double lambda = 0.01) {
        if (isFrozen) return;
        
        JFET newJfet;
        newJfet.instanceName = name;
        newJfet.drain = D;
        newJfet.gate = G;
        newJfet.source = S;
        newJfet.modelName = model;
        newJfet.isNChannel = isNChannel;
        newJfet.beta = beta;
        newJfet.Vto = Vto;
        newJfet.lambda = lambda;
        
        jfets.push_back(newJfet);
        updateNodeCount(D); updateNodeCount(G); updateNodeCount(S);
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

// ============================================================================
// SUBCIRCUIT DEFINITIONS (Hierarchical IC Packages)
// ============================================================================

/*
 * SubCircuitDefinition - Represents the internal circuitry of an IC package.
 * Used for hierarchical simulation where ICs like ULN2003A expand to their
 * internal Darlington transistors, resistors, and protection diodes.
 *
 * Example: ULN2003A has 16 pins, 7 Darlington channels internally.
 * Pin mapping connects external package pins to internal node indices.
 */
struct SubCircuitDefinition {
    std::string name;                         // e.g., "ULN2003A"
    std::string description;                  // Human-readable description
    
    // Package Information
    int pinCount = 0;                         // Total pins (e.g., 16 for DIP-16)
    std::vector<std::string> pinNames;        // e.g., {"1B", "2B", ..., "GND", "VCC"}
    
    // Internal Circuitry (uses local node indices starting from 0)
    TensorBlock internalBlock;
    
    // Pin-to-Internal-Node Mapping
    // Key: pin index (0-based), Value: internal node index
    std::map<int, int> pinToInternalNode;
    
    // Helper to get internal node for a given pin
    int getInternalNode(int pinIndex) const {
        auto it = pinToInternalNode.find(pinIndex);
        return (it != pinToInternalNode.end()) ? it->second : -1;
    }
};

/*
 * ICPackageInstance - An instance of a SubCircuitDefinition placed on the schematic.
 * Stores the mapping from package pins to global netlist nodes.
 */
struct ICPackageInstance {
    std::string instanceName;                 // e.g., "U1"
    std::string subcircuitName;               // Reference to SubCircuitDefinition
    
    // Pin-to-Global-Node Mapping (set during netlist generation)
    std::map<int, int> pinToGlobalNode;       // Key: pin index, Value: global node
    
    // Position (for GUI)
    double x = 0.0, y = 0.0;
    double rotation = 0.0;
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
    
    // Global Simulation Context (Phase 2.9)
    SimulationEnvironment environment;

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
    void addVoltageSource(int nPos, int nNeg, double V, std::string type = "") {
        globalBlock.addVoltageSource(nPos, nNeg, V, type);
        if (nPos > numGlobalNodes) numGlobalNodes = nPos;
        if (nNeg > numGlobalNodes) numGlobalNodes = nNeg;
    }
    void addCurrentSource(int nPos, int nNeg, double I) {
        globalBlock.addCurrentSource(nPos, nNeg, I);
        if (nPos > numGlobalNodes) numGlobalNodes = nPos;
        if (nNeg > numGlobalNodes) numGlobalNodes = nNeg;
    }
    void addPowerRail(const std::string& name, int node, double V, double ripple = 0.0) {
        globalBlock.addPowerRail(name, node, V, ripple);
        if (node > numGlobalNodes) numGlobalNodes = node;
    }
    void addSignalSource(const std::string& name, int nPos, int nNeg, 
                         SignalSource::WaveformType type, double amp, double freq) {
        globalBlock.addSignalSource(name, nPos, nNeg, type, amp, freq);
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
    void addBusProbe(const std::string& name, const std::vector<int>& nodes) {
        BusProbe bp;
        bp.name = name;
        bp.bit_width = nodes.size();
        bp.nodeIndices = nodes;
        globalBlock.busProbes.push_back(bp);
    }
};

// ============================================================================
// SUBCIRCUIT EXTRACTION (Phase 1.7 - Component Abstraction)
// ============================================================================

/**
 * PortInfo - Describes an external port for subcircuit extraction.
 * Used to specify which nodes in the source netlist become pins in the subcircuit.
 */
struct PortInfo {
    std::string pinName;      // Name for the pin (e.g., "VIN", "VOUT", "GND")
    int globalNodeId;         // Node ID in the source TensorNetlist
    bool isPowerPin = false;  // True for VDD/VSS pins (affects symbol rendering)
};

/**
 * extractSubCircuit
 * 
 * Converts a flat TensorNetlist into a reusable SubCircuitDefinition.
 * 
 * @param source      The TensorNetlist to extract from (typically a Bench/Block design)
 * @param name        Name for the new subcircuit (e.g., "MyInverter")
 * @param description Human-readable description
 * @param ports       List of PortInfo specifying which nodes become external pins
 * @return            A SubCircuitDefinition ready for library storage
 * 
 * The algorithm:
 * 1. Create a new TensorBlock from the source's globalBlock.
 * 2. Build a node remapping table (global node → local node).
 * 3. Port nodes are mapped to sequential local indices (1, 2, 3...).
 * 4. Internal-only nodes are mapped after port nodes.
 * 5. All component node references are rewritten using the remap table.
 * 6. The resulting SubCircuitDefinition has pinToInternalNode set correctly.
 */
inline SubCircuitDefinition extractSubCircuit(
    const TensorNetlist& source,
    const std::string& name,
    const std::string& description,
    const std::vector<PortInfo>& ports)
{
    SubCircuitDefinition result;
    result.name = name;
    result.description = description;
    result.pinCount = static_cast<int>(ports.size());
    
    // Build the pin names and port-to-node mapping
    // Local node 0 = internal ground (if present)
    // Local nodes 1..N = external pins
    // Local nodes N+1.. = internal nodes
    
    std::map<int, int> globalToLocal;  // Global node → Local node
    int nextLocalNode = 1;  // Start at 1 (0 is reserved for ground)
    
    // Step 1: Map port nodes first (in order specified)
    for (size_t i = 0; i < ports.size(); ++i) {
        const auto& port = ports[i];
        result.pinNames.push_back(port.pinName);
        
        if (globalToLocal.find(port.globalNodeId) == globalToLocal.end()) {
            globalToLocal[port.globalNodeId] = nextLocalNode;
            result.pinToInternalNode[static_cast<int>(i)] = nextLocalNode;
            nextLocalNode++;
        } else {
            // Port references an already-mapped node (shared pin)
            result.pinToInternalNode[static_cast<int>(i)] = globalToLocal[port.globalNodeId];
        }
    }
    
    // Step 2: Collect all nodes used in the source netlist
    std::set<int> allNodes;
    const auto& block = source.globalBlock;
    
    for (const auto& r : block.resistors) {
        allNodes.insert(r.nodeTerminal1);
        allNodes.insert(r.nodeTerminal2);
    }
    for (const auto& c : block.capacitors) {
        allNodes.insert(c.nodePlate1);
        allNodes.insert(c.nodePlate2);
    }
    for (const auto& l : block.inductors) {
        allNodes.insert(l.nodeCoil1);
        allNodes.insert(l.nodeCoil2);
    }
    for (const auto& vs : block.voltageSources) {
        allNodes.insert(vs.nodePositive);
        allNodes.insert(vs.nodeNegative);
    }
    for (const auto& cs : block.currentSources) {
        allNodes.insert(cs.nodePositive);
        allNodes.insert(cs.nodeNegative);
    }
    for (const auto& d : block.diodes) {
        allNodes.insert(d.anode);
        allNodes.insert(d.cathode);
    }
    for (const auto& m : block.mosfets) {
        allNodes.insert(m.drain);
        allNodes.insert(m.gate);
        allNodes.insert(m.source);
        allNodes.insert(m.body);
    }
    for (const auto& b : block.bjts) {
        allNodes.insert(b.nodeCollector);
        allNodes.insert(b.base);
        allNodes.insert(b.emitter);
    }
    
    // Step 3: Map internal-only nodes
    for (int node : allNodes) {
        if (node == 0) continue;  // Ground stays as 0
        if (globalToLocal.find(node) == globalToLocal.end()) {
            globalToLocal[node] = nextLocalNode++;
        }
    }
    globalToLocal[0] = 0;  // Ground is always 0
    
    // Helper lambda to remap a node
    auto remap = [&](int globalNode) -> int {
        auto it = globalToLocal.find(globalNode);
        return (it != globalToLocal.end()) ? it->second : 0;
    };
    
    // Step 4: Copy and remap all components
    TensorBlock& out = result.internalBlock;
    out.name = name + "_internal";
    out.numInternalNodes = nextLocalNode - 1;
    
    // Resistors
    for (const auto& r : block.resistors) {
        Resistor newR = r;
        newR.nodeTerminal1 = remap(r.nodeTerminal1);
        newR.nodeTerminal2 = remap(r.nodeTerminal2);
        out.resistors.push_back(newR);
    }
    
    // Capacitors
    for (const auto& c : block.capacitors) {
        Capacitor newC = c;
        newC.nodePlate1 = remap(c.nodePlate1);
        newC.nodePlate2 = remap(c.nodePlate2);
        out.capacitors.push_back(newC);
    }
    
    // Inductors
    for (const auto& l : block.inductors) {
        Inductor newL = l;
        newL.nodeCoil1 = remap(l.nodeCoil1);
        newL.nodeCoil2 = remap(l.nodeCoil2);
        out.inductors.push_back(newL);
    }
    
    // Voltage Sources (Note: These become internal sources in the subcircuit)
    for (const auto& vs : block.voltageSources) {
        VoltageSource newVS = vs;
        newVS.nodePositive = remap(vs.nodePositive);
        newVS.nodeNegative = remap(vs.nodeNegative);
        out.voltageSources.push_back(newVS);
    }
    
    // Current Sources
    for (const auto& cs : block.currentSources) {
        CurrentSource newCS = cs;
        newCS.nodePositive = remap(cs.nodePositive);
        newCS.nodeNegative = remap(cs.nodeNegative);
        out.currentSources.push_back(newCS);
    }
    
    // Diodes
    for (const auto& d : block.diodes) {
        Diode newD = d;
        newD.anode = remap(d.anode);
        newD.cathode = remap(d.cathode);
        out.diodes.push_back(newD);
    }
    
    // MOSFETs
    for (const auto& m : block.mosfets) {
        Mosfet newM = m;
        newM.drain = remap(m.drain);
        newM.gate = remap(m.gate);
        newM.source = remap(m.source);
        newM.body = remap(m.body);
        out.mosfets.push_back(newM);
    }
    
    // BJTs
    for (const auto& b : block.bjts) {
        BJT newB = b;
        newB.nodeCollector = remap(b.nodeCollector);
        newB.base = remap(b.base);
        newB.emitter = remap(b.emitter);
        out.bjts.push_back(newB);
    }
    
    // Note: Probes, BusProbes, and other runtime-only structures are NOT copied
    // to the subcircuit definition (they are measurement tools, not circuit elements).
    
    return result;
}
