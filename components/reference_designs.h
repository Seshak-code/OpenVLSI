#pragma once
#include "acutesim_engine/netlist/circuit.h"
#include <string>
#include <cmath>

/*
 * reference_designs.h
 * Phase 1.0.5: Analog Designer MVP - Reference Design Library
 *
 * Provides parametric generators for common analog building blocks:
 * - LNA (Low Noise Amplifier)
 * - OpAmp (Two-Stage Miller OTA)
 * - Bandgap Reference (Brokaw Cell)
 *
 * Each generator returns a TensorBlock that can be:
 * 1. Simulated directly in AcuteSim
 * 2. Exported to SPICE for golden verification vs ngspice
 * 3. Used as few-shot examples for AI circuit generation
 */

namespace ReferenceLibrary {

/**
 * LNA_Cascode_Params
 * Parameters for Cascode LNA with Inductive Degeneration
 */
struct LNA_Cascode_Params {
    // Transistor sizing
    double W_cs = 50e-6;        // Common-source transistor width [m]
    double L_cs = 180e-9;       // Common-source transistor length [m]
    double W_cg = 50e-6;        // Common-gate transistor width [m]
    double L_cg = 180e-9;       // Common-gate transistor length [m]
    
    // Inductors
    double L_source = 1e-9;     // Source degeneration inductor [H]
    double L_gate = 10e-9;      // Gate inductor for input match [H]
    double L_drain = 5e-9;      // Drain load inductor [H]
    
    // Bias
    double Vbias_cg = 1.2;      // Cascode gate bias [V]
    double Vdd = 1.8;           // Supply voltage [V]
    
    // Matching network (simplified)
    double C_in = 1e-12;        // Input coupling capacitor [F]
    double C_out = 1e-12;       // Output coupling capacitor [F]
};

/**
 * createLNA_Cascode
 * 
 * Generates a Cascode LNA with Inductive Source Degeneration.
 * 
 * Topology:
 *   VDD ──┬── L_drain ──┬── Vout
 *         │             │
 *         └── M_cg ─────┘   (Cascode: drain=Vout, gate=Vbias, source=internal)
 *              │
 *              M_cs          (Common Source: drain=M_cg.source, gate=Vin, source=L_s)
 *              │
 *         L_source
 *              │
 *             GND
 *
 * Nodes:
 *   1: VIN (RF input)
 *   2: VOUT (RF output)
 *   3: VDD (Supply)
 *   4: VBIAS_CG (Cascode bias)
 *   0: GND
 */
inline TensorBlock createLNA_Cascode(const LNA_Cascode_Params& p = {}) {
    TensorBlock block;
    block.name = "LNA_CASCODE";
    
    // Internal nodes
    const int VIN = 1;
    const int VOUT = 2;
    const int VDD = 3;
    const int VBIAS_CG = 4;
    const int N_GATE_CS = 5;     // After input matching
    const int N_DRAIN_CS = 6;    // CS drain / CG source
    const int N_SOURCE_CS = 7;   // After source inductor
    const int N_DRAIN_CG = 8;    // CG drain (before L_drain)
    
    // Input matching: VIN -> C_in -> L_gate -> M_cs.gate
    block.addCapacitor("C_IN", VIN, N_GATE_CS, p.C_in);
    block.addInductor("L_GATE", N_GATE_CS, N_SOURCE_CS, p.L_gate);  // Simplified: gate to source
    
    // Source degeneration inductor
    block.addInductor("L_SOURCE", N_SOURCE_CS, 0, p.L_source);
    
    // Common-Source transistor (M_cs)
    block.addMosfet("M_CS", N_DRAIN_CS, N_GATE_CS, N_SOURCE_CS, 0, "nfet_lna", p.W_cs, p.L_cs);
    
    // Cascode transistor (M_cg)
    block.addMosfet("M_CG", N_DRAIN_CG, VBIAS_CG, N_DRAIN_CS, 0, "nfet_lna", p.W_cg, p.L_cg);
    
    // Drain load inductor
    block.addInductor("L_DRAIN", VDD, N_DRAIN_CG, p.L_drain);
    
    // Output coupling
    block.addCapacitor("C_OUT", N_DRAIN_CG, VOUT, p.C_out);
    
    // Bias voltage source for cascode
    block.addVoltageSource(VBIAS_CG, 0, p.Vbias_cg);
    
    // Supply
    block.addVoltageSource(VDD, 0, p.Vdd);
    
    block.numInternalNodes = 8;
    return block;
}

/**
 * OpAmp_TwoStage_Params
 * Parameters for Two-Stage Miller-Compensated OTA
 */
struct OpAmp_TwoStage_Params {
    // Differential pair (M1, M2)
    double W_diff = 10e-6;
    double L_diff = 500e-9;
    
    // Active load (M3, M4 - PMOS current mirror)
    double W_load = 5e-6;
    double L_load = 500e-9;
    
    // Tail current source (M5)
    double W_tail = 20e-6;
    double L_tail = 1e-6;
    
    // Second stage (M6 - CS amp, M7 - PMOS load)
    double W_cs2 = 50e-6;
    double L_cs2 = 500e-9;
    double W_load2 = 25e-6;
    double L_load2 = 500e-9;
    
    // Miller compensation
    double C_c = 2e-12;         // Compensation capacitor [F]
    double R_nulling = 1e3;     // Nulling resistor (optional) [Ω]
    
    // Bias
    double I_tail = 100e-6;     // Tail current [A]
    double Vdd = 1.8;           // Supply [V]
};

/**
 * createOpAmp_TwoStage
 * 
 * Generates a classic Two-Stage Miller-Compensated OTA.
 * 
 * Stage 1: Differential pair (M1, M2) with PMOS active load (M3, M4)
 * Stage 2: Common-source amplifier (M6) with PMOS load (M7)
 * Compensation: Miller capacitor C_c between stage 1 output and stage 2 output
 *
 * Nodes:
 *   1: VIN+ (Non-inverting input)
 *   2: VIN- (Inverting input)
 *   3: VOUT
 *   4: VDD
 *   5: VSS (GND)
 */
inline TensorBlock createOpAmp_TwoStage(const OpAmp_TwoStage_Params& p = {}) {
    TensorBlock block;
    block.name = "OPAMP_TWO_STAGE";
    
    // External nodes
    const int VIN_P = 1;
    const int VIN_N = 2;
    const int VOUT = 3;
    const int VDD = 4;
    const int VSS = 0;
    
    // Internal nodes
    const int N_TAIL = 5;        // Tail current node
    const int N_DRAIN_M1 = 6;    // M1 drain (stage 1 output+)
    const int N_DRAIN_M2 = 7;    // M2 drain (stage 1 output-)
    const int N_BIAS_TAIL = 8;   // Tail bias voltage
    const int N_STAGE1_OUT = 9;  // Stage 1 single-ended output (M2 drain)
    
    // --- Stage 1: Differential Pair ---
    // Tail current source (M5 - NMOS)
    // For simplicity, use ideal current source
    block.addCurrentSource(N_TAIL, VSS, p.I_tail);
    
    // Differential pair (M1, M2 - NMOS)
    block.addMosfet("M1", N_DRAIN_M1, VIN_P, N_TAIL, VSS, "nfet_ota", p.W_diff, p.L_diff);
    block.addMosfet("M2", N_DRAIN_M2, VIN_N, N_TAIL, VSS, "nfet_ota", p.W_diff, p.L_diff);
    
    // Active load (M3, M4 - PMOS current mirror)
    block.addMosfet("M3", N_DRAIN_M1, N_DRAIN_M1, VDD, VDD, "pfet_ota", p.W_load, p.L_load);
    block.addMosfet("M4", N_DRAIN_M2, N_DRAIN_M1, VDD, VDD, "pfet_ota", p.W_load, p.L_load);
    
    // Stage 1 output is M2 drain (single-ended)
    // N_STAGE1_OUT = N_DRAIN_M2
    
    // --- Stage 2: CS Amplifier ---
    // M6 - NMOS common source
    block.addMosfet("M6", VOUT, N_DRAIN_M2, VSS, VSS, "nfet_ota", p.W_cs2, p.L_cs2);
    
    // M7 - PMOS active load
    block.addMosfet("M7", VOUT, N_DRAIN_M1, VDD, VDD, "pfet_ota", p.W_load2, p.L_load2);
    
    // --- Miller Compensation ---
    // C_c between stage 1 output and stage 2 output
    block.addCapacitor("C_C", N_DRAIN_M2, VOUT, p.C_c);
    
    // Optional: Nulling resistor in series with C_c (not implemented for simplicity)
    
    // --- Supply ---
    block.addVoltageSource(VDD, VSS, p.Vdd);
    
    block.numInternalNodes = 9;
    return block;
}

/**
 * Bandgap_Brokaw_Params
 * Parameters for Brokaw Bandgap Reference
 */
struct Bandgap_Brokaw_Params {
    double R1 = 10e3;           // PTAT resistor [Ω]
    double R2 = 10e3;           // Feedback resistor [Ω]
    double R3 = 1e3;            // Emitter resistor ratio element [Ω]
    int emitter_ratio = 8;      // Q2 has N times the emitter area of Q1
    double Vdd = 3.3;           // Supply voltage [V]
};

/**
 * createBandgap_Brokaw
 * 
 * Generates a Brokaw Bandgap Reference.
 * 
 * Principle: V_REF = V_BE + K * V_T * ln(N)
 * Where N is the emitter area ratio and K is set by resistor ratio.
 * At proper sizing, temperature coefficients cancel: dV_REF/dT ≈ 0
 *
 * Simplified topology:
 *   - Q1, Q2: Vertical BJTs with emitter area ratio N
 *   - Feedback loop sets equal collector currents
 *   - V_REF ≈ 1.25V (bandgap of silicon)
 *
 * Nodes:
 *   1: V_REF (Output reference voltage)
 *   2: VDD (Supply)
 *   0: GND
 */
inline TensorBlock createBandgap_Brokaw(const Bandgap_Brokaw_Params& p = {}) {
    TensorBlock block;
    block.name = "BANDGAP_BROKAW";
    
    // External nodes
    const int V_REF = 1;
    const int VDD = 2;
    const int GND = 0;
    
    // Internal nodes
    const int N_COLL_Q1 = 3;     // Q1 collector
    const int N_COLL_Q2 = 4;     // Q2 collector
    const int N_BASE = 5;        // Shared base node
    const int N_EMIT_Q1 = 6;     // Q1 emitter
    const int N_EMIT_Q2 = 7;     // Q2 emitter
    const int N_MID = 8;         // Middle of resistor divider
    
    // --- BJT Core ---
    // Q1: Standard emitter area (Is = 1e-15)
    block.addBJT("Q1", N_COLL_Q1, N_BASE, N_EMIT_Q1, "npn_bandgap", true, 100.0, 1.0, 1e-15, 0.02585);
    
    // Q2: N times emitter area (Is = N * 1e-15)
    double Is_Q2 = p.emitter_ratio * 1e-15;
    block.addBJT("Q2", N_COLL_Q2, N_BASE, N_EMIT_Q2, "npn_bandgap", true, 100.0, 1.0, Is_Q2, 0.02585);
    
    // --- Resistor Network ---
    // R3: Creates ΔV_BE across it (emitter of Q2 to emitter of Q1)
    block.addResistor("R3", N_EMIT_Q1, N_EMIT_Q2, p.R3);
    
    // R1: PTAT current generator (from V_REF to Q1 emitter)
    block.addResistor("R1", V_REF, N_EMIT_Q1, p.R1);
    
    // R2: Sets output voltage (from V_REF to Q2 emitter)
    block.addResistor("R2", V_REF, N_EMIT_Q2, p.R2);
    
    // --- Idealized OpAmp Feedback (simplified as VCVS) ---
    // In real design, this is an error amplifier that forces V_C1 = V_C2
    // For simulation, we add a high-gain VCVS or direct connection
    // Simplified: Force collectors to V_REF (ideal current mirror behavior)
    block.addResistor("R_C1", VDD, N_COLL_Q1, 1e3);
    block.addResistor("R_C2", VDD, N_COLL_Q2, 1e3);
    
    // Connect collectors to V_REF (simplified output)
    block.addResistor("R_FB", N_COLL_Q1, V_REF, 100);
    
    // Base connection (self-biased in Brokaw topology)
    block.addResistor("R_BASE", N_BASE, N_COLL_Q2, 10e3);
    
    // --- Supply ---
    block.addVoltageSource(VDD, GND, p.Vdd);
    
    // Emitter to ground for Q2
    block.addResistor("R_GND", N_EMIT_Q2, GND, 1e3);
    
    block.numInternalNodes = 8;
    return block;
}

/**
 * SimulationProfile
 * Pre-configured testbench settings for each reference design
 */
struct SimulationProfile {
    std::string name;
    std::string description;
    
    // DC Operating Point
    bool runDC = true;
    
    // Transient Analysis
    bool runTransient = false;
    double t_stop = 1e-6;       // [s]
    double t_step = 1e-9;       // [s]
    
    // AC Analysis
    bool runAC = false;
    double f_start = 1e3;       // [Hz]
    double f_stop = 10e9;       // [Hz]
    int points_per_decade = 10;
    
    // Noise Analysis (future)
    bool runNoise = false;
    
    // Temperature Sweep
    bool runTempSweep = false;
    double temp_start = -40.0;  // [°C]
    double temp_stop = 125.0;   // [°C]
    double temp_step = 5.0;     // [°C]
};

inline SimulationProfile getLNA_Profile() {
    SimulationProfile p;
    p.name = "LNA_CASCODE";
    p.description = "AC gain and S-parameter analysis for LNA";
    p.runDC = true;
    p.runAC = true;
    p.f_start = 1e6;
    p.f_stop = 10e9;
    p.points_per_decade = 20;
    return p;
}

inline SimulationProfile getOpAmp_Profile() {
    SimulationProfile p;
    p.name = "OPAMP_TWO_STAGE";
    p.description = "Open-loop gain, phase margin, unity-gain bandwidth";
    p.runDC = true;
    p.runAC = true;
    p.f_start = 1;
    p.f_stop = 100e6;
    p.points_per_decade = 10;
    p.runTransient = true;
    p.t_stop = 10e-6;
    p.t_step = 10e-9;
    return p;
}

inline SimulationProfile getBandgap_Profile() {
    SimulationProfile p;
    p.name = "BANDGAP_BROKAW";
    p.description = "Temperature sweep for VREF stability";
    p.runDC = true;
    p.runTempSweep = true;
    p.temp_start = -40.0;
    p.temp_stop = 125.0;
    p.temp_step = 5.0;
    return p;
}

} // namespace ReferenceLibrary
