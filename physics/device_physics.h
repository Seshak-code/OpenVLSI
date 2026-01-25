#pragma once
#include <cmath>
#include <algorithm>

/*
 * device_physics.h - Pure Constitutive Equations for Semiconductor Devices
 *
 * This file contains "kernel-ready" physics functions. They are:
 * - Stateless (no side effects)
 * - Inline (for CPU performance / GPU transpilation)
 * - Bit-accurate (no approximations unless specified)
 * - Templated (for Automatic Differentiation / Dual Numbers)
 *
 * These functions can be called from:
 * 1. The MNA stamping logic (to linearize devices for NR)
 * 2. The KCL Residual Auditor (to verify physical conservation)
 * 3. Future GPU compute shaders (WGSL/GLSL)
 */

// ============================================================================
// DIODE (PN JUNCTION) PHYSICS
// ============================================================================

template <typename T>
struct DiodeParams 
{
    T Is;  // Saturation Current [A] at Tnom
    T N;   // Emission Coefficient (1.0 for ideal)
    T Vt;  // Thermal Voltage [V] (kT/q ≈ 25.85mV @ 300K)
    T Rs;   // Series Parasitic Resistance [Ω]
    
    // Thermal Parameters
    T Tnom; // Nominal Temperature (usually 300.15K)
    T Temp; // Operating Temperature [K]
    T Eg;   // Bandgap Energy [eV] (1.11 for Si)
    T Xti;  // IS temperature exponent (3.0 for pn junction)

    // Calculate Is at operating temperature
    T Is_effective() const {
        if (Temp == Tnom) return Is;
        T ratio = Temp / Tnom;
        // Eg is in eV, Vt is in V. V = kT/q. Need to be careful with Boltzmann constant units if using raw k.
        // Simplified relations used in SPICE:
        // Is(T) = Is * (T/Tnom)^(Xti/N) * exp(...) 
        // Note: Xti usually 3.0.
        return Is * std::pow(ratio, Xti / N) * std::exp((Eg / (0.02585)) * (1.0 - Tnom/Temp)); // using Vt_nom approx
    }
};

/**
 * diode_current
 * Exact Shockley Equation: I = Is * (exp(Vd / (N*Vt)) - 1)
 * Exact Shockley Equation including Series Resistance iteration would require implicit solve.
 * Here we assume Rs is handled by external stamps (StampedResistor).
 */
template <typename T, typename P>
inline T diode_current(T vd, const DiodeParams<P>& p) 
{
    // Note: Implicit type promotion via operator overloads in dual.h
    using std::exp;
    return p.Is * (exp(vd / (p.N * p.Vt)) - 1.0);
}

/**
 * diode_current_clamped
 * Safe version for NR iteration. Clamps exponent to prevent inf/nan.
 */
template <typename T, typename P>
inline T diode_current_clamped(T vd, const DiodeParams<P>& p, double max_arg = 30.0) 
{
    // NASA Optimization: Keep params as scalars (double) to avoid Dual overhead
    auto n_vt = p.N * p.Vt;
    T arg = vd / n_vt;
    
    // Linear Continuation to avoid zero-gradient (singularity) and overflow
    // If arg > max_arg, assume linear slope matching the derivative at max_arg.
    if (arg > max_arg) {
        // I(max) = Is * (exp(max) - 1)
        // G(max) = (Is / n_vt) * exp(max)
        using std::exp;
    double exp_max = exp(max_arg);
        auto i_max = p.Is * (exp_max - 1.0);
        auto g_max = (p.Is / n_vt) * exp_max;
        
        T delta = vd - (n_vt * max_arg);
        return i_max + delta * g_max;
    }
    
    using std::exp;
    return p.Is * (exp(arg) - 1.0);
}

/**
 * pnjlim - SPICE-Style PN Junction Voltage Limiting
 * 
 * Limits the change in junction voltage during Newton-Raphson iteration to 
 * prevent the exponential from overshooting and causing non-convergence.
 * 
 * Algorithm (SPICE3f5):
 *   - If new voltage is increasing significantly, limit the step.
 *   - vcrit = Vt * ln(Vt / (sqrt(2) * Is))  (critical turn-on voltage)
 *   - Allow larger steps when turning on, smaller when conducting.
 * 
 * @param vnew  Proposed new junction voltage
 * @param vold  Previous junction voltage
 * @param vt    Thermal voltage (kT/q ≈ 26mV at room temp)
 * @param vcrit Critical voltage (pre-computed for efficiency)
 * @return      Limited junction voltage
 */
inline double pnjlim(double vnew, double vold, double vt, double vcrit) {
    double vlim = vnew;
    
    // Bypass limiting if we are in the linear continuation region (defined as > 30*Vt in physics)
    // In this region, the device behaves as a linear resistor, so Newton-Raphson is stable/exact.
    if (vold > 30.0 * vt && vnew > 30.0 * vt) {
        return vnew;
    }
    
    // Bypass limiting in deep reverse bias (constant current region)
    if (vold < -5.0 * vt && vnew < -5.0 * vt) {
        return vnew;
    }

    if (vnew > vcrit && std::abs(vnew - vold) > 2.0 * vt) {
        // Large forward step - apply limiting
        if (vold > 0.0) {
            double arg = 1.0 + (vnew - vold) / vt;
            if (arg > 0.0) {
                vlim = vold + vt * std::log(arg);
            } else {
                vlim = vcrit;
            }
        } else {
            vlim = vt * std::log(vnew / vt);
        }
    } else if (vnew < -5.0 * vt) {
        // Deep reverse bias - limit negative swing
        // Prevent excessive negative voltages that could cause numerical issues
        vlim = std::max(vnew, vold - 5.0 * vt);
    }
    
    return vlim;
}

/**
 * compute_vcrit - Compute critical voltage for pnjlim
 * vcrit = Vt * ln(Vt / (sqrt(2) * Is))
 */
inline double compute_vcrit(double vt, double Is) {
    return vt * std::log(vt / (std::sqrt(2.0) * Is));
}

// NOTE: Manual derivative functions (conductance) removed.
// Use Dual<double> with diode_current to get {I, G}.

// ============================================================================
// MOSFET (LEVEL 1 SHICHMAN-HODGES) PHYSICS
// ============================================================================

template <typename T>
struct MosfetParams 
{
    T Kp;     // Process transconductance [A/V^2] at Tnom
    T Vth;    // Threshold Voltage [V]
    T lambda; // Channel Length Modulation [1/V]
    T W;      // Channel Width [m]
    T L;      // Channel Length [m]
    
    // Thermal
    T Temp;
    T Tnom;
};

enum class MosfetRegion { CUTOFF, LINEAR, SATURATION };

/**
 * mosfet_region
 * Determines operating region from terminal voltages. (NMOS)
 * For Dual types, this uses the primal value for comparisons.
 */
template <typename T, typename P>
inline MosfetRegion mosfet_region(T vgs, T vds, const MosfetParams<P>& p) 
{
    T vov = vgs - p.Vth;
    if (vov <= 0.0) return MosfetRegion::CUTOFF;
    if (vds < vov)  return MosfetRegion::LINEAR;
    return MosfetRegion::SATURATION;
}

/**
 * mosfet_ids
 * Level 1 Drain-Source Current (NMOS).
 */
template <typename T, typename P>
inline T mosfet_ids(T vgs, T vds, const MosfetParams<P>& p) 
{
    T K = 0.5 * p.Kp * (p.W / p.L);
    T vov = vgs - p.Vth;

    if (vov <= 0.0) 
        return 0.0; // Cutoff

    // We can just rely on vds < vov check, or call mosfet_region.
    // Re-evaluating condition for branchless-ish optimization if T is SIMD?
    // For scalar/dual, straightforward if-else is fine.
    
    if (vds < vov) 
    {
        // Linear Region: Ids = K * (2*Vov*Vds - Vds^2)
        return K * (2.0 * vov * vds - vds * vds);
    } 
    else 
    {
        // Saturation: Ids = K * Vov^2 * (1 + lambda * Vds)
        return K * vov * vov * (1.0 + p.lambda * vds);
    }
}

// ============================================================================
// BJT (EBERS-MOLL / GUMMEL-POON L1) PHYSICS
// ============================================================================

template <typename T>
struct BJTParams 
{
    T Is;     // Transport Saturation Current [A]
    T BetaF;  // Forward Current Gain
    T BetaR;  // Reverse Current Gain
    T Vt;     // Thermal Voltage [V]
    
    // Thermal
    T Temp;
    T Tnom;
    T Xtb;    // Beta temperature exponent
    T Eg;     // Bandgap
    T Xti;    // IS temperature exponent
};

struct BJTCurrents {
    double Ic; // Collector Current
    double Ib; // Base Current
    double Ie; // Emitter Current
    
    // Gradients for linearization (g_ij = dI_i / dV_j)
    // Indexes: C=0, B=1, E=2
    double g_cc, g_cb, g_ce; 
    double g_bc, g_bb, g_be;
    double g_ec, g_eb, g_ee;
};

/**
 * bjt_ebers_moll
 * Computes currents and conductance matrix for a BJT.
 * This function returns a struct with values (for RHS) and gradients (for Matrix).
 * Currently implemented using Scalar Double arithmetic for optimal performance 
 * (manual derivative derivation), but can be templated if needed.
 */
inline BJTCurrents bjt_ebers_moll(double v_c, double v_b, double v_e, bool isNPN, const BJTParams<double>& p)
{
    // 1. Determine local voltages relative to Base (Vbc, Vbe)
    // NPN: Vbe = Vb - Ve, Vbc = Vb - Vc
    // PNP: Vbe = Ve - Vb, Vbc = Vc - Vb
    
    double v_be, v_bc;
    double sign = isNPN ? 1.0 : -1.0;

    v_be = sign * (v_b - v_e);
    v_bc = sign * (v_b - v_c);

    // 2. Compute Diode Currents with Linear Continuation (Branchless, GPU-Ready)
    // For Vj > Vcrit: I = I(Vcrit) + G(Vcrit) * (Vj - Vcrit)
    // This avoids exponential overflow and provides smooth derivatives.
    
    const double Vcrit = 30.0 * p.Vt;  // Critical voltage for linear region (Reduced from 40.0 for robustness)
    const double GMIN = 1e-12;         // Minimum conductance for cutoff
    
    // Branchless clamping using std::min/std::max (GPU-friendly)
    auto linear_continuation_diode = [&](double v_j) -> std::pair<double, double> {
        double arg = v_j / p.Vt;
        double arg_clamped = std::min(arg, 30.0);  // Branchless clamp
        
        using std::exp;
        double exp_val = exp(arg_clamped);
        double i_base = p.Is * (exp_val - 1.0);
        double g_base = (p.Is / p.Vt) * exp_val;
        
        // Linear continuation for v_j > Vcrit
        double delta = std::max(0.0, v_j - Vcrit);  // Branchless
        double i_ext = g_base * delta;
        
        double i_total = i_base + i_ext;
        double g_total = std::max(GMIN, g_base);  // Ensure minimum conductance
        
        return {i_total, g_total};
    };
    
    auto [i_f, g_f] = linear_continuation_diode(v_be);
    auto [i_r, g_r] = linear_continuation_diode(v_bc);

    // 3. Terminal Currents (Ebers-Moll)
    // I_C = I_F - I_R/alpha_R  =>  I_F - I_R * (1 + 1/BetaR)
    // I_E = - (I_F/alpha_F - I_R) => - (I_F * (1 + 1/BetaF) - I_R)
    // I_B = (I_F/beta_F) + (I_R/beta_R)
    
    // Using common form:
    // I_CT = I_F - I_R (Transport Current)
    // I_C = I_CT - I_R/BetaR
    // I_B = I_F/BetaF + I_R/BetaR
    // I_E = -I_C - I_B
    
    double invBetaF = 1.0 / p.BetaF;
    double invBetaR = 1.0 / p.BetaR;
    
    double i_ct = i_f - i_r;
    double i_base_component_f = i_f * invBetaF;
    double i_base_component_r = i_r * invBetaR;

    double ic_local = i_ct - i_base_component_r;
    double ib_local = i_base_component_f + i_base_component_r;
    double ie_local = -ic_local - ib_local;
    
    // 4. Derivatives (Conductances)
    // We need dIx / dV_C, dIx / dV_B, dIx / dV_E
    // Chain rule: dI/dV_x = (dI/dVbe * dVbe/dVx) + (dI/dVbc * dVbc/dVx)
    
    // NPN partials:
    // dVbe/dVb = 1, dVbe/dVe = -1, dVbe/dVc = 0
    // dVbc/dVb = 1, dVbc/dVe = 0,  dVbc/dVc = -1
    
    double dIf_dVbe = g_f;
    double dIr_dVbc = g_r;
    
    double dIct_dVbe = dIf_dVbe;
    double dIct_dVbc = -dIr_dVbc;
    
    double dIbf_dVbe = dIf_dVbe * invBetaF; // Base current fwd component
    double dIbr_dVbc = dIr_dVbc * invBetaR; // Base current rev component
    
    // Local derivatives (w.r.t internal Vbe, Vbc)
    // Ic = Ict - Ir/BetaR
    double dIc_dVbe = dIct_dVbe;       // = g_f
    double dIc_dVbc = dIct_dVbc - dIbr_dVbc; // = -g_r - g_r/BetaR = -g_r(1+1/Br)
    
    // Ib = If/BetaF + Ir/BetaR
    double dIb_dVbe = dIbf_dVbe;       // = g_f/BetaF
    double dIb_dVbc = dIbr_dVbc;       // = g_r/BetaR
    
    // Ie = -Ic - Ib
    double dIe_dVbe = -(dIc_dVbe + dIb_dVbe);
    double dIe_dVbc = -(dIc_dVbc + dIb_dVbc);
    
    // Map to Terminal Voltages (NPN specific first)
    // dIdx / dVc = dIdx/dVbc * dVbc/dVc = dIdx/dVbc * (-1)
    // dIdx / dVe = dIdx/dVbe * dVbe/dVe = dIdx/dVbe * (-1)
    // dIdx / dVb = dIdx/dVbe * 1 + dIdx/dVbc * 1
    
    BJTCurrents res;
    res.Ic = sign * ic_local;
    res.Ib = sign * ib_local;
    res.Ie = sign * ie_local;

    // The sign factor handles NPN/PNP current direction, but conductance is always positive for passive-ish/stable regions? 
    // Wait, conductance is just slope.
    
    // Terminal Conductances (Common for NPN and PNP due to sign cancellation)
    // dIdx / dVc = dIdx/dVbc * dVbc/dVc (NPN: -1, PNP: 1 but Ic is -1)
    // dIdx / dVe = dIdx/dVbe * dVbe/dVe (NPN: -1, PNP: 1 but Ie is -1)
    
    res.g_cc = -dIc_dVbc; 
    res.g_cb = dIc_dVbe + dIc_dVbc;
    res.g_ce = -dIc_dVbe;
    
    res.g_bc = -dIb_dVbc;
    res.g_bb = dIb_dVbe + dIb_dVbc;
    res.g_be = -dIb_dVbe;
    
    res.g_ec = -dIe_dVbc;
    res.g_eb = dIe_dVbe + dIe_dVbc;
    res.g_ee = -dIe_dVbe;
    
    return res;
}

// ============================================================================
// JFET (SHICHMAN-HODGES MODEL) PHYSICS
// ============================================================================

template <typename T>
struct JFETParams 
{
    T Beta;     // Transconductance parameter [A/V^2] = IDSS/Vto^2
    T Vto;      // Pinch-off voltage [V] (negative for N-ch, positive for P-ch)
    T lambda;   // Channel-length modulation [1/V]
};

enum class JFETRegion { CUTOFF, LINEAR, SATURATION };

/**
 * jfet_region
 * Determines operating region from terminal voltages. (N-channel)
 * For P-channel, the signs are inverted before calling.
 */
template <typename T, typename P>
inline JFETRegion jfet_region(T vgs, T vds, const JFETParams<P>& p) 
{
    T vov = vgs - p.Vto; // For N-ch, Vto is negative, so vov = vgs - (-2) = vgs + 2
    if (vov <= 0.0) return JFETRegion::CUTOFF;
    if (vds < vov)  return JFETRegion::LINEAR;
    return JFETRegion::SATURATION;
}

/**
 * jfet_ids
 * Shichman-Hodges Drain-Source Current (N-channel reference).
 * For P-channel, caller should negate vgs, vds before and negate result after.
 */
template <typename T, typename P>
inline T jfet_ids(T vgs, T vds, const JFETParams<P>& p) 
{
    T vov = vgs - p.Vto; // Overdrive voltage

    if (vov <= 0.0) 
        return 0.0; // Cutoff

    if (vds < vov) 
    {
        // Linear Region: Ids = Beta * (2*Vov*Vds - Vds^2) * (1 + lambda*Vds)
        return p.Beta * (2.0 * vov * vds - vds * vds) * (1.0 + p.lambda * vds);
    } 
    else 
    {
        // Saturation: Ids = Beta * Vov^2 * (1 + lambda * Vds)
        return p.Beta * vov * vov * (1.0 + p.lambda * vds);
    }
}

/**
 * jfet_linearization
 * Returns {Ids, gm, gds} at the operating point for NR stamping.
 */
struct JFETLinear {
    double Ids;  // Drain current
    double gm;   // dIds/dVgs (transconductance)
    double gds;  // dIds/dVds (output conductance)
};

inline JFETLinear jfet_linearization(double vgs, double vds, bool isNChannel, const JFETParams<double>& p) 
{
    // P-channel: negate voltages for symmetric computation
    double sign = isNChannel ? 1.0 : -1.0;
    double vgs_eff = sign * vgs;
    double vds_eff = sign * vds;
    
    double vov = vgs_eff - p.Vto;
    
    JFETLinear res = {0.0, 0.0, 0.0};
    
    if (vov <= 0.0) {
        // Cutoff: small GMIN for stability
        res.gds = 1e-12;
        return res;
    }
    
    if (vds_eff < vov) {
        // Linear region
        // Ids = Beta * (2*vov*vds - vds^2) * (1 + lambda*vds)
        double base = 2.0 * vov * vds_eff - vds_eff * vds_eff;
        double clm = 1.0 + p.lambda * vds_eff;
        res.Ids = p.Beta * base * clm;
        
        // gm = dIds/dVgs = Beta * 2 * vds * clm (since d(vov)/d(vgs) = 1)
        res.gm = p.Beta * 2.0 * vds_eff * clm;
        
        // gds = dIds/dVds = Beta * (2*vov - 2*vds) * clm + Beta * base * lambda
        res.gds = p.Beta * (2.0 * vov - 2.0 * vds_eff) * clm + p.Beta * base * p.lambda;
    } else {
        // Saturation region
        // Ids = Beta * vov^2 * (1 + lambda * vds)
        double vov2 = vov * vov;
        double clm = 1.0 + p.lambda * vds_eff;
        res.Ids = p.Beta * vov2 * clm;
        
        // gm = dIds/dVgs = Beta * 2 * vov * clm
        res.gm = p.Beta * 2.0 * vov * clm;
        
        // gds = dIds/dVds = Beta * vov^2 * lambda
        res.gds = p.Beta * vov2 * p.lambda;
    }
    
    // Ensure minimum conductance for numerical stability
    res.gds = std::max(res.gds, 1e-12);
    res.gm = std::max(res.gm, 1e-12);
    
    // For P-channel, negate current (current flows opposite)
    res.Ids *= sign;
    
    return res;
}

