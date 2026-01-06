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
    T Is;  // Saturation Current [A]
    T N;   // Emission Coefficient (1.0 for ideal)
    T Vt;  // Thermal Voltage [V] (kT/q ≈ 25.85mV @ 300K)
};

/**
 * diode_current
 * Exact Shockley Equation: I = Is * (exp(Vd / (N*Vt)) - 1)
 */
template <typename T, typename P>
inline T diode_current(T vd, const DiodeParams<P>& p) 
{
    // Note: Implicit type promotion via operator overloads in dual.h
    return p.Is * (std::exp(vd / (p.N * p.Vt)) - 1.0);
}

/**
 * diode_current_clamped
 * Safe version for NR iteration. Clamps exponent to prevent inf/nan.
 */
template <typename T, typename P>
inline T diode_current_clamped(T vd, const DiodeParams<P>& p, double max_arg = 40.0) 
{
    // NASA Optimization: Keep params as scalars (double) to avoid Dual overhead
    auto n_vt = p.N * p.Vt;
    T arg = vd / n_vt;
    
    // Linear Continuation to avoid zero-gradient (singularity) and overflow
    // If arg > max_arg, assume linear slope matching the derivative at max_arg.
    if (arg > max_arg) {
        // I(max) = Is * (exp(max) - 1)
        // G(max) = (Is / n_vt) * exp(max)
        double exp_max = std::exp(max_arg);
        auto i_max = p.Is * (exp_max - 1.0);
        auto g_max = (p.Is / n_vt) * exp_max;
        
        T delta = vd - (n_vt * max_arg);
        return i_max + delta * g_max;
    }
    
    return p.Is * (std::exp(arg) - 1.0);
}

// NOTE: Manual derivative functions (conductance) removed.
// Use Dual<double> with diode_current to get {I, G}.

// ============================================================================
// MOSFET (LEVEL 1 SHICHMAN-HODGES) PHYSICS
// ============================================================================

template <typename T>
struct MosfetParams 
{
    T Kp;     // Process transconductance [A/V^2]
    T Vth;    // Threshold Voltage [V]
    T lambda; // Channel Length Modulation [1/V]
    T W;      // Channel Width [m]
    T L;      // Channel Length [m]
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

// NOTE: Manual gm/gds functions removed. Use Dual<double>.
