#include "physics_tensors.h"
#include "kernel_dispatch.h"
#include "../../components/circuit.h"
#include <iostream>
#include <cmath>
#include <algorithm>

/*
 * physics_tensors.cpp - Implementation of AoS->SoA conversion & Kernel Dispatch.
 */

using namespace acutesim::compute;

// ============================================================================
// KERNEL IMPLEMENTATIONS (GENERIC CPU)
// ============================================================================

void batchDiodePhysics_generic(DiodeTensor& tensor, const std::vector<double>& voltages) {
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
        double Vcrit = 30.0 * n_vt;
        
        double arg = tensor.v_d[i] / n_vt;
        
        // Linear continuation (branchless-style using std::min/max)
        double arg_clamped = std::min(arg, 30.0);
        double exp_arg = std::exp(arg_clamped);
        
        double i_base = Is * (exp_arg - 1.0);
        double g_base = (Is / n_vt) * exp_arg;
        
        double delta = std::max(0.0, tensor.v_d[i] - Vcrit);
        tensor.i_d[i] = i_base + delta * g_base;
        tensor.g_d[i] = std::max(GMIN, g_base);
    }
}

void batchMosfetPhysics_generic(MosfetTensor& tensor, const std::vector<double>& voltages) {
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

void batchBJTPhysics_generic(BJTTensor& tensor, const std::vector<double>& voltages) {
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
        
        const double Vcrit = 30.0 * Vt;

        auto linear_continuation_diode = [&](double v_j) -> std::pair<double, double> {
            double arg = v_j / Vt;
            double arg_clamped = std::min(arg, 30.0);
            
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

// ============================================================================
// PUBLIC DISPATCH WRAPPERS
// ============================================================================

void batchDiodePhysics(DiodeTensor& tensor, const std::vector<double>& voltages) {
    KernelDispatcher::get().batch_diode_physics(tensor, voltages);
}

void batchMosfetPhysics(MosfetTensor& tensor, const std::vector<double>& voltages) {
    KernelDispatcher::get().batch_mosfet_physics(tensor, voltages);
}

void batchBJTPhysics(BJTTensor& tensor, const std::vector<double>& voltages) {
    KernelDispatcher::get().batch_bjt_physics(tensor, voltages);
}

// ============================================================================
// AOS -> SOA CONVERSION
// ============================================================================

TensorizedBlock tensorizeBlock(const TensorBlock& block) {
    TensorizedBlock result;
    
    // Convert Resistors
    result.resistors.nodes1.reserve(block.resistors.size());
    result.resistors.nodes2.reserve(block.resistors.size());
    result.resistors.R.reserve(block.resistors.size());
    
    // Safety Clamps for Fixed-Point Solver (1e9 Scale)
    // Max Conductance G_max = 2.0 S => R_min = 0.5 Ohm
    // Min Conductance G_min = 1 nS => R_max = 1 GigaOhm
    const double R_MIN_SAFE = 0.5; 
    const double R_MAX_SAFE = 1.0e9;

    for (const auto& r : block.resistors) {
        double safeR = r.resistance_ohms;
        if (safeR < R_MIN_SAFE) {
            std::cerr << "[WARNING] Resistor clamped: " << safeR << " -> " << R_MIN_SAFE << " (Min Safe R)\n";
            safeR = R_MIN_SAFE;
        } else if (safeR > R_MAX_SAFE) {
             // Not strictly dangerous for crash, but prevents 0 conductance
            safeR = R_MAX_SAFE;
        }
        result.resistors.push(r.nodeTerminal1, r.nodeTerminal2, safeR);
    }
    
    // Convert Capacitors
    for (const auto& c : block.capacitors) {
        result.capacitors.push(c.nodePlate1, c.nodePlate2, c.capacitance_farads);
    }
    
    // Convert Diodes
    result.diodes.reserve(block.diodes.size());
    for (const auto& d : block.diodes) {
        // Rs default 0.0, Temp default passed
        result.diodes.push(d.anode, d.cathode, 
                           d.saturationCurrent_I_S_A, 
                           d.emissionCoefficient_N, 
                           d.thermalVoltage_V_T_V,
                           0.0, // Rs
                           d.temperature_K);
    }
    
    // Convert MOSFETs
    result.mosfets.reserve(block.mosfets.size());
    for (const auto& m : block.mosfets) {
        // Defaults: Kp=200e-6, Vth=0.7, Labmda=0.02, Temp=passed
        result.mosfets.push(m.drain, m.gate, m.source, m.body, 
                           m.w, m.l,
                           200e-6, 0.7, 0.02, 
                           m.temperature_K);
    }
    
    // Convert BJTs
    result.bjts.reserve(block.bjts.size());
    for (const auto& q : block.bjts) {
        result.bjts.push(q.nodeCollector, q.base, q.emitter,
                        q.saturationCurrent_I_S_A,
                        q.betaF, q.betaR,
                        q.thermalVoltage_V_T_V,
                        q.isNPN,
                        q.temperature_K);
    }
    
    return result;
}
