#pragma once
#include <vector>
#include <cstddef>

/*
 * physics_tensors.h - Structure-of-Arrays (SoA) Tensor Data Structures
 *
 * These structures enable batch-parallel processing of device physics:
 * - SIMD vectorization on CPU (AVX2/AVX-512)
 * - GPU compute shader dispatch (WebGPU/CUDA)
 * - Superposition decomposition (multi-source analysis)
 *
 * Each tensor stores device parameters in contiguous arrays for cache-optimal access.
 */

// ============================================================================
// DIODE TENSOR
// ============================================================================

struct DiodeTensor {
    // Node indices (global node IDs)
    std::vector<int> anodes;
    std::vector<int> cathodes;
    
    // Device parameters
    std::vector<double> Is;   // Saturation current [A]
    std::vector<double> N;    // Emission coefficient
    std::vector<double> Vt;   // Thermal voltage [V]
    
    // Thermal parameters (static model)
    std::vector<double> Rs;   // Series resistance [Ω] (parasitic)
    std::vector<double> Temp; // Operating temperature [K]
    
    // Iteration state (updated each NR pass)
    std::vector<double> v_d;  // Voltage across diode [V]
    std::vector<double> i_d;  // Current through diode [A]
    std::vector<double> g_d;  // Linearized conductance [S]
    
    size_t size() const { return anodes.size(); }
    
    void reserve(size_t n) {
        anodes.reserve(n); cathodes.reserve(n);
        Is.reserve(n); N.reserve(n); Vt.reserve(n);
        Rs.reserve(n); Temp.reserve(n);
        v_d.reserve(n); i_d.reserve(n); g_d.reserve(n);
    }
    
    void clear() {
        anodes.clear(); cathodes.clear();
        Is.clear(); N.clear(); Vt.clear();
        Rs.clear(); Temp.clear();
        v_d.clear(); i_d.clear(); g_d.clear();
    }
    
    void push(int anode, int cathode, double is, double n, double vt, 
              double rs = 0.0, double temp = 300.0) {
        anodes.push_back(anode);
        cathodes.push_back(cathode);
        Is.push_back(is);
        N.push_back(n);
        Vt.push_back(vt);
        Rs.push_back(rs);
        Temp.push_back(temp);
        v_d.push_back(0.0);
        i_d.push_back(0.0);
        g_d.push_back(1e-12); // GMIN default
    }
};

// ============================================================================
// MOSFET TENSOR  
// ============================================================================

struct MosfetTensor {
    // Node indices
    std::vector<int> drains;
    std::vector<int> gates;
    std::vector<int> sources;
    std::vector<int> bodies;
    
    // Device parameters
    std::vector<double> W;       // Channel width [m]
    std::vector<double> L;       // Channel length [m]
    std::vector<double> Kp;      // Transconductance parameter [A/V²]
    std::vector<double> Vth;     // Threshold voltage [V]
    std::vector<double> lambda;  // Channel length modulation [1/V]
    
    // Thermal parameters
    std::vector<double> Temp;    // Operating temperature [K]
    
    // Iteration state
    std::vector<double> vgs;     // Gate-source voltage [V]
    std::vector<double> vds;     // Drain-source voltage [V]
    std::vector<double> ids;     // Drain current [A]
    std::vector<double> gm;      // Transconductance [S]
    std::vector<double> gds;     // Output conductance [S]
    
    size_t size() const { return drains.size(); }
    
    void reserve(size_t n) {
        drains.reserve(n); gates.reserve(n); sources.reserve(n); bodies.reserve(n);
        W.reserve(n); L.reserve(n); Kp.reserve(n); Vth.reserve(n); lambda.reserve(n);
        Temp.reserve(n);
        vgs.reserve(n); vds.reserve(n); ids.reserve(n); gm.reserve(n); gds.reserve(n);
    }
    
    void clear() {
        drains.clear(); gates.clear(); sources.clear(); bodies.clear();
        W.clear(); L.clear(); Kp.clear(); Vth.clear(); lambda.clear();
        Temp.clear();
        vgs.clear(); vds.clear(); ids.clear(); gm.clear(); gds.clear();
    }
    
    void push(int d, int g, int s, int b, double w, double l, 
              double kp = 200e-6, double vth = 0.7, double lam = 0.02, double temp = 300.0) {
        drains.push_back(d);
        gates.push_back(g);
        sources.push_back(s);
        bodies.push_back(b);
        W.push_back(w);
        L.push_back(l);
        Kp.push_back(kp);
        Vth.push_back(vth);
        lambda.push_back(lam);
        Temp.push_back(temp);
        vgs.push_back(0.0);
        vds.push_back(0.0);
        ids.push_back(0.0);
        gm.push_back(0.0);
        gds.push_back(1e-12);
    }
};

// ============================================================================
// BJT TENSOR
// ============================================================================

struct BJTTensor {
    // Node indices
    std::vector<int> collectors;
    std::vector<int> bases;
    std::vector<int> emitters;
    
    // Device parameters
    std::vector<double> Is;      // Saturation current [A]
    std::vector<double> BetaF;   // Forward current gain
    std::vector<double> BetaR;   // Reverse current gain
    std::vector<double> Vt;      // Thermal voltage [V]
    std::vector<bool> isNPN;     // true = NPN, false = PNP
    
    // Thermal parameters
    std::vector<double> Temp;    // Operating temperature [K]
    
    // Iteration state (terminal currents)
    std::vector<double> Ic;      // Collector current [A]
    std::vector<double> Ib;      // Base current [A]
    std::vector<double> Ie;      // Emitter current [A]
    
    // Conductance matrix (3x3 per device, flattened)
    std::vector<double> g_cc, g_cb, g_ce;
    std::vector<double> g_bc, g_bb, g_be;
    std::vector<double> g_ec, g_eb, g_ee;
    
    size_t size() const { return collectors.size(); }
    
    void reserve(size_t n) {
        collectors.reserve(n); bases.reserve(n); emitters.reserve(n);
        Is.reserve(n); BetaF.reserve(n); BetaR.reserve(n); Vt.reserve(n); isNPN.reserve(n);
        Temp.reserve(n);
        Ic.reserve(n); Ib.reserve(n); Ie.reserve(n);
        g_cc.reserve(n); g_cb.reserve(n); g_ce.reserve(n);
        g_bc.reserve(n); g_bb.reserve(n); g_be.reserve(n);
        g_ec.reserve(n); g_eb.reserve(n); g_ee.reserve(n);
    }
    
    void clear() {
        collectors.clear(); bases.clear(); emitters.clear();
        Is.clear(); BetaF.clear(); BetaR.clear(); Vt.clear(); isNPN.clear();
        Temp.clear();
        Ic.clear(); Ib.clear(); Ie.clear();
        g_cc.clear(); g_cb.clear(); g_ce.clear();
        g_bc.clear(); g_bb.clear(); g_be.clear();
        g_ec.clear(); g_eb.clear(); g_ee.clear();
    }
    
    void push(int c, int b, int e, double is, double betaF, double betaR, 
              double vt, bool npn, double temp = 300.0) {
        collectors.push_back(c);
        bases.push_back(b);
        emitters.push_back(e);
        Is.push_back(is);
        BetaF.push_back(betaF);
        BetaR.push_back(betaR);
        Vt.push_back(vt);
        isNPN.push_back(npn);
        Temp.push_back(temp);
        
        // Initialize currents to zero
        Ic.push_back(0.0);
        Ib.push_back(0.0);
        Ie.push_back(0.0);
        
        // Initialize conductances to GMIN
        double gmin = 1e-12;
        g_cc.push_back(gmin); g_cb.push_back(0.0); g_ce.push_back(0.0);
        g_bc.push_back(0.0); g_bb.push_back(gmin); g_be.push_back(0.0);
        g_ec.push_back(0.0); g_eb.push_back(0.0); g_ee.push_back(gmin);
    }
};

// ============================================================================
// RESISTOR TENSOR (Linear - for completeness)
// ============================================================================

struct ResistorTensor {
    std::vector<int> nodes1;
    std::vector<int> nodes2;
    std::vector<double> R;  // Resistance [Ω]
    
    size_t size() const { return nodes1.size(); }
    
    void push(int n1, int n2, double r) {
        nodes1.push_back(n1);
        nodes2.push_back(n2);
        R.push_back(r);
    }
    
    void clear() {
        nodes1.clear(); nodes2.clear(); R.clear();
    }
};

// ============================================================================
// CAPACITOR TENSOR (for transient)
// ============================================================================

struct CapacitorTensor {
    std::vector<int> plates1;
    std::vector<int> plates2;
    std::vector<double> C;         // Capacitance [F]
    std::vector<double> v_prev;    // Voltage at t-1
    std::vector<double> i_prev;    // Current at t-1
    
    size_t size() const { return plates1.size(); }
    
    void push(int p1, int p2, double c) {
        plates1.push_back(p1);
        plates2.push_back(p2);
        C.push_back(c);
        v_prev.push_back(0.0);
        i_prev.push_back(0.0);
    }
    
    void clear() {
        plates1.clear(); plates2.clear(); C.clear();
        v_prev.clear(); i_prev.clear();
    }
};

// ============================================================================
// INDUCTOR TENSOR (for transient)
// ============================================================================

struct InductorTensor {
    std::vector<int> coil1;
    std::vector<int> coil2;
    std::vector<double> L;         // Inductance [H]
    std::vector<double> i_prev;    // Current at t-1
    std::vector<double> v_prev;    // Voltage at t-1
    
    size_t size() const { return coil1.size(); }
    
    void push(int c1, int c2, double l) {
        coil1.push_back(c1);
        coil2.push_back(c2);
        L.push_back(l);
        i_prev.push_back(0.0);
        v_prev.push_back(0.0);
    }
    
    void clear() {
        coil1.clear(); coil2.clear(); L.clear();
        i_prev.clear(); v_prev.clear();
    }
};

// ============================================================================
// UNIFIED TENSOR BLOCK
// ============================================================================

/**
 * TensorizedBlock - Unified container for all device tensors.
 * This replaces the AoS `TensorBlock` for solver-side operations.
 */
struct TensorizedBlock {
    ResistorTensor resistors;
    CapacitorTensor capacitors;
    InductorTensor inductors;
    DiodeTensor diodes;
    MosfetTensor mosfets;
    BJTTensor bjts;
    
    void clear() {
        resistors.clear();
        capacitors.clear();
        inductors.clear();
        diodes.clear();
        mosfets.clear();
        bjts.clear();
    }
};

// ============================================================================
// CONVERSION FUNCTIONS (AoS -> SoA)
// ============================================================================

// Forward declaration: TensorBlock is defined in circuit.h
struct TensorBlock;

/**
 * Convert a TensorBlock (AoS) to TensorizedBlock (SoA).
 * This enables efficient batch processing on the solver side.
 */
TensorizedBlock tensorizeBlock(const struct TensorBlock& block);

// ============================================================================
// BATCH PHYSICS KERNELS (Vectorizable)
// ============================================================================

#include <cmath>

/**
 * Batch Diode Physics - Evaluates all diodes in a tensor simultaneously.
 * Updates: i_d, g_d arrays in place.
 * 
 * @param tensor  The diode tensor (modified in place)
 * @param voltages  Global node voltage vector
 */
inline void batchDiodePhysics(DiodeTensor& tensor, const std::vector<double>& voltages) {
    size_t n = tensor.size();
    const double GMIN = 1e-12;
    
    for (size_t i = 0; i < n; ++i) {
        int nA = tensor.anodes[i];
        int nC = tensor.cathodes[i];
        
        double v_a = (nA > 0 && nA <= (int)voltages.size()) ? voltages[nA - 1] : 0.0;
        double v_c = (nC > 0 && nC <= (int)voltages.size()) ? voltages[nC - 1] : 0.0;
        
        tensor.v_d[i] = v_a - v_c;
        
        double Is = tensor.Is[i];
        double N = tensor.N[i];
        double Vt = tensor.Vt[i];
        double n_vt = N * Vt;
        double Vcrit = 40.0 * n_vt;
        
        double arg = tensor.v_d[i] / n_vt;
        
        // Linear continuation (branchless-style using std::min/max)
        double arg_clamped = std::min(arg, 40.0);
        double exp_arg = std::exp(arg_clamped);
        
        double i_base = Is * (exp_arg - 1.0);
        double g_base = (Is / n_vt) * exp_arg;
        
        double delta = std::max(0.0, tensor.v_d[i] - Vcrit);
        tensor.i_d[i] = i_base + delta * g_base;
        tensor.g_d[i] = std::max(GMIN, g_base);
    }
}

/**
 * Batch MOSFET Physics - Evaluates all MOSFETs in a tensor simultaneously.
 * Updates: ids, gm, gds arrays in place.
 */
inline void batchMosfetPhysics(MosfetTensor& tensor, const std::vector<double>& voltages) {
    size_t n = tensor.size();
    
    for (size_t i = 0; i < n; ++i) {
        int nD = tensor.drains[i];
        int nG = tensor.gates[i];
        int nS = tensor.sources[i];
        
        double v_d = (nD > 0 && nD <= (int)voltages.size()) ? voltages[nD - 1] : 0.0;
        double v_g = (nG > 0 && nG <= (int)voltages.size()) ? voltages[nG - 1] : 0.0;
        double v_s = (nS > 0 && nS <= (int)voltages.size()) ? voltages[nS - 1] : 0.0;
        
        tensor.vgs[i] = v_g - v_s;
        tensor.vds[i] = v_d - v_s;
        
        double K = 0.5 * tensor.Kp[i] * (tensor.W[i] / tensor.L[i]);
        double vov = tensor.vgs[i] - tensor.Vth[i];
        
        if (vov <= 0.0) {
            // Cutoff
            tensor.ids[i] = 0.0;
            tensor.gm[i] = 0.0;
            tensor.gds[i] = 1e-12; // GMIN
        } else if (tensor.vds[i] < vov) {
            // Linear region
            double vds = tensor.vds[i];
            tensor.ids[i] = K * (2.0 * vov * vds - vds * vds);
            tensor.gm[i] = 2.0 * K * vds;
            tensor.gds[i] = 2.0 * K * (vov - vds);
        } else {
            // Saturation
            double lam = tensor.lambda[i];
            tensor.ids[i] = K * vov * vov * (1.0 + lam * tensor.vds[i]);
            tensor.gm[i] = 2.0 * K * vov * (1.0 + lam * tensor.vds[i]);
            tensor.gds[i] = K * vov * vov * lam;
        }
        
        if (tensor.gds[i] < 1e-12) tensor.gds[i] = 1e-12;
    }
}

/**
 * Batch BJT Physics - Evaluates all BJTs in a tensor simultaneously.
 * Updates: Ic, Ib, Ie, and conductance matrix arrays in place.
 */
inline void batchBJTPhysics(BJTTensor& tensor, const std::vector<double>& voltages) {
    size_t n = tensor.size();
    const double GMIN = 1e-12;
    
    for (size_t i = 0; i < n; ++i) {
        int nC = tensor.collectors[i];
        int nB = tensor.bases[i];
        int nE = tensor.emitters[i];
        
        double v_c = (nC > 0 && nC <= (int)voltages.size()) ? voltages[nC - 1] : 0.0;
        double v_b = (nB > 0 && nB <= (int)voltages.size()) ? voltages[nB - 1] : 0.0;
        double v_e = (nE > 0 && nE <= (int)voltages.size()) ? voltages[nE - 1] : 0.0;
        
        double sign = tensor.isNPN[i] ? 1.0 : -1.0;
        double v_be = sign * (v_b - v_e);
        double v_bc = sign * (v_b - v_c);
        
        double Is = tensor.Is[i];
        double Vt = tensor.Vt[i];
        double BetaF = tensor.BetaF[i];
        double BetaR = tensor.BetaR[i];
        
        const double Vcrit = 40.0 * Vt;

        auto linear_continuation_diode = [&](double v_j) -> std::pair<double, double> {
            double arg = v_j / Vt;
            double arg_clamped = std::min(arg, 40.0);
            
            double exp_val = std::exp(arg_clamped);
            double i_base = Is * (exp_val - 1.0);
            double g_base = (Is / Vt) * exp_val;
            
            double delta = std::max(0.0, v_j - Vcrit);
            double i_ext = g_base * delta;
            
            return {i_base + i_ext, std::max(GMIN, g_base)};
        };
        
        auto [i_f, g_f] = linear_continuation_diode(v_be);
        auto [i_r, g_r] = linear_continuation_diode(v_bc);
        
        double invBetaF = 1.0 / BetaF;
        double invBetaR = 1.0 / BetaR;
        
        double i_ct = i_f - i_r;
        double ic_local = i_ct - i_r * invBetaR;
        double ib_local = i_f * invBetaF + i_r * invBetaR;
        double ie_local = -ic_local - ib_local;
        
        tensor.Ic[i] = sign * ic_local;
        tensor.Ib[i] = sign * ib_local;
        tensor.Ie[i] = sign * ie_local;
        
        // Conductances
        double dIc_dVbe = g_f;
        double dIc_dVbc = -g_r * (1.0 + invBetaR);
        double dIb_dVbe = g_f * invBetaF;
        double dIb_dVbc = g_r * invBetaR;
        double dIe_dVbe = -(dIc_dVbe + dIb_dVbe);
        double dIe_dVbc = -(dIc_dVbc + dIb_dVbc);
        
        // Terminal Conductances (Common for NPN and PNP)
        // Apply chain rule: dI/dVc = -dI/dVbc, dI/dVe = -dI/dVbe, dI/dVb = dI/dVbe + dI/dVbc
        tensor.g_cc[i] = -dIc_dVbc;
        tensor.g_cb[i] = dIc_dVbe + dIc_dVbc;
        tensor.g_ce[i] = -dIc_dVbe;
        tensor.g_bc[i] = -dIb_dVbc;
        tensor.g_bb[i] = dIb_dVbe + dIb_dVbc;
        tensor.g_be[i] = -dIb_dVbe;
        tensor.g_ec[i] = -dIe_dVbc;
        tensor.g_eb[i] = dIe_dVbe + dIe_dVbc;
        tensor.g_ee[i] = -dIe_dVbe;
        
        // CRITICAL: Add GMIN to diagonal terms to prevent singular Jacobian
        // This is essential for convergence when BJT is in cutoff/low current
        tensor.g_cc[i] = std::max(tensor.g_cc[i], GMIN);
        tensor.g_bb[i] = std::max(tensor.g_bb[i], GMIN);
        tensor.g_ee[i] = std::max(tensor.g_ee[i], GMIN);
    }
}

