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
 *
 * These functions can be called from:
 * 1. The MNA stamping logic (to linearize devices for NR)
 * 2. The KCL Residual Auditor (to verify physical conservation)
 * 3. Future GPU compute shaders (WGSL/GLSL)
 */

// ============================================================================
// DIODE (PN JUNCTION) PHYSICS
// ============================================================================

struct DiodeParams 
{
    double Is;  // Saturation Current [A]
    double N;   // Emission Coefficient (1.0 for ideal)
    double Vt;  // Thermal Voltage [V] (kT/q ≈ 25.85mV @ 300K)
};

/**
 * diode_current
 * Exact Shockley Equation: I = Is * (exp(Vd / (N*Vt)) - 1)
 */
inline double diode_current(double vd, const DiodeParams& p) 
{
    double n_vt = p.N * p.Vt;
    return p.Is * (std::exp(vd / n_vt) - 1.0);
}

/**
 * diode_current_clamped
 * Safe version for NR iteration. Clamps exponent to prevent inf/nan.
 */
inline double diode_current_clamped(double vd, const DiodeParams& p, double max_arg = 40.0) 
{
    double n_vt = p.N * p.Vt;
    double arg = std::min(vd / n_vt, max_arg);
    return p.Is * (std::exp(arg) - 1.0);
}

/**
 * diode_conductance
 * Jacobian for NR: dI/dV = (Is / (N*Vt)) * exp(Vd / (N*Vt))
 */
inline double diode_conductance(double vd, const DiodeParams& p) 
{
    double n_vt = p.N * p.Vt;
    return (p.Is / n_vt) * std::exp(vd / n_vt);
}

/**
 * diode_conductance_clamped
 * Safe version for NR iteration.
 */
inline double diode_conductance_clamped(double vd, const DiodeParams& p, double max_arg = 40.0) 
{
    double n_vt = p.N * p.Vt;
    double arg = std::min(vd / n_vt, max_arg);
    return (p.Is / n_vt) * std::exp(arg);
}

// ============================================================================
// MOSFET (LEVEL 1 SHICHMAN-HODGES) PHYSICS
// ============================================================================

struct MosfetParams 
{
    double Kp;     // Process transconductance [A/V^2]
    double Vth;    // Threshold Voltage [V]
    double lambda; // Channel Length Modulation [1/V]
    double W;      // Channel Width [m]
    double L;      // Channel Length [m]
};

enum class MosfetRegion { CUTOFF, LINEAR, SATURATION };

/**
 * mosfet_region
 * Determines operating region from terminal voltages. (NMOS)
 */
inline MosfetRegion mosfet_region(double vgs, double vds, const MosfetParams& p) 
{
    double vov = vgs - p.Vth;
    if (vov <= 0.0) return MosfetRegion::CUTOFF;
    if (vds < vov)  return MosfetRegion::LINEAR;
    return MosfetRegion::SATURATION;
}

/**
 * mosfet_ids
 * Level 1 Drain-Source Current (NMOS).
 */
inline double mosfet_ids(double vgs, double vds, const MosfetParams& p) 
{
    double K = 0.5 * p.Kp * (p.W / p.L);
    double vov = vgs - p.Vth;

    if (vov <= 0.0) 
        return 0.0; // Cutoff

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

/**
 * mosfet_gm
 * Transconductance: dIds/dVgs
 */
inline double mosfet_gm(double vgs, double vds, const MosfetParams& p)
{
    double K = 0.5 * p.Kp * (p.W / p.L);
    double vov = vgs - p.Vth;
    if (vov <= 0.0) return 0.0;

    if (vds < vov) 
        return 2.0 * K * vds; // Linear
    else
        return 2.0 * K * vov * (1.0 + p.lambda * vds); // Saturation
}

/**
 * mosfet_gds
 * Output Conductance: dIds/dVds
 */
inline double mosfet_gds(double vgs, double vds, const MosfetParams& p)
{
    double K = 0.5 * p.Kp * (p.W / p.L);
    double vov = vgs - p.Vth;
    if (vov <= 0.0) return 0.0;

    if (vds < vov)
        return 2.0 * K * (vov - vds); // Linear
    else
        return K * vov * vov * p.lambda; // Saturation (CLM term)
}
