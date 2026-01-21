#pragma once
#include "linalg.h"
#include <vector>
#include <cmath>
#include <algorithm>

/*
 * stamps.h - Component Stamping Logic
 * This file contains the low-level physics-to-matrix mapping for each circuit element.
 */

inline void stampResistor(int nodeI, int nodeJ, double resistance, MatrixConstructor& matrixBuilder)
{
    double g = 1.0 / resistance;
    if (nodeI != 0) 
    {
        matrixBuilder.add(nodeI - 1, nodeI - 1, g);
        if (nodeJ != 0) 
            matrixBuilder.add(nodeI - 1, nodeJ - 1, -g);
    }
    if (nodeJ != 0) 
    {
        matrixBuilder.add(nodeJ - 1, nodeJ - 1, g);
        if (nodeI != 0)
            matrixBuilder.add(nodeJ - 1, nodeI - 1, -g);
    }
}

inline void stampVoltageSourceAsNorton(int nodePos, int nodeNeg, double voltage, 
                                        MatrixConstructor& matrixBuilder, std::vector<double>& rhsVector)
{
    // std::cout << "StampVSource: " << nodePos << " " << nodeNeg << " V=" << voltage << "\n";

    // We treat independent voltage sources as high-conductance Norton equivalents 
    // to keep the MNA matrix entirely in the voltage domain.
    double r_int = 1e-3;
    double g_int = 1.0 / r_int;
    double i_eq = voltage * g_int;
    
    if (nodePos != 0) {
        matrixBuilder.add(nodePos - 1, nodePos - 1, g_int);
        rhsVector[nodePos - 1] += i_eq;
    }
    if (nodeNeg != 0) {
        matrixBuilder.add(nodeNeg - 1, nodeNeg - 1, g_int);
        rhsVector[nodeNeg - 1] -= i_eq;
    }
    if (nodePos != 0 && nodeNeg != 0) {
        matrixBuilder.add(nodePos - 1, nodeNeg - 1, -g_int);
        matrixBuilder.add(nodeNeg - 1, nodePos - 1, -g_int);
    }
}

/**
 * Stamp Power Rail (PDN Model)
 * Models: V_ideal -> ESR -> Node -> C_decap -> GND
 * 
 * At Node:
 * 1. Conductance G_esr = 1/ESR to V_ideal
 * 2. Conductance G_cap = 2C/h to GND
 * 
 * Matrix[Node, Node] += G_esr + G_cap
 * RHS[Node] += V_ideal * G_esr + I_cap_eq
 */
inline void stampPowerRail(int node, double V_nom, double V_ripple, double freq,
                          double ESR, double C, double h, double time,
                          double v_prev, double i_prev_cap,
                          MatrixConstructor& matrixBuilder, std::vector<double>& rhsVector)
{
    if (node == 0) return; // Cannot drive Ground
    
    // 1. Calculate Source Voltage V(t)
    double V_source = V_nom;
    if (V_ripple > 1e-9) {
        V_source += (V_ripple / 2.0) * std::sin(2.0 * M_PI * freq * time);
    }
    
    // 2. Conductances
    double G_esr = 1.0 / ESR;
    double G_cap_eq = (h > 1e-15) ? (2.0 * C / h) : 0.0;
    
    // 3. Current Sources
    // I_from_source = (V_source - V_node) * G_esr => Stamped as Isrc = V_source * G_esr (Norton)
    double I_norton_src = V_source * G_esr;
    
    // I_cap_eq (Trapezoidal)
    // i_cap(t) = G_cap_eq * (v_node - 0) + I_eq_hist
    // I_eq_hist = -G_cap_eq * v_prev - i_prev_cap
    // MNA: G * v = I_injected
    // I_injected_from_cap_model = -I_eq_hist = G_cap_eq * v_prev + i_prev_cap
    double I_cap_inj = 0.0;
    if (h > 1e-15) {
        I_cap_inj = G_cap_eq * v_prev + i_prev_cap;
    }
    
    // 4. Stamp Matrix
    matrixBuilder.add(node - 1, node - 1, G_esr + G_cap_eq);
    
    // 5. Stamp RHS
    rhsVector[node - 1] += I_norton_src + I_cap_inj;
}

inline void stampCapacitorTrapezoidal(int nodeI, int nodeJ, double C, double h, 
                                       double v_prev, double i_prev, 
                                       MatrixConstructor& matrixBuilder, std::vector<double>& rhsVector)
{
    // Trapezoidal Companion Model (Thevenin/Norton equivalent)
    double G_eq = (2.0 * C) / h;
    double I_eq = -G_eq * v_prev - i_prev;

    if (nodeI != 0) 
    {
        matrixBuilder.add(nodeI - 1, nodeI - 1, G_eq);
        if (nodeJ != 0) 
            matrixBuilder.add(nodeI - 1, nodeJ - 1, -G_eq);
    }
    if (nodeJ != 0) 
    {
        matrixBuilder.add(nodeJ - 1, nodeJ - 1, G_eq);
        if (nodeI != 0) 
            matrixBuilder.add(nodeJ - 1, nodeI - 1, -G_eq);
    }

    if (nodeI != 0) 
        rhsVector[nodeI - 1] -= I_eq;
    if (nodeJ != 0) 
        rhsVector[nodeJ - 1] += I_eq;
}

/**
 * Inductor Trapezoidal Companion Model
 * Physics: v_L = L * di/dt
 * Companion Model (Norton equivalent):
 *   R_eq = 2*L / h (equivalent resistance)
 *   I_eq = i_prev + (v_prev / R_eq)
 * Stamps R_eq in parallel with I_eq current source.
 */
inline void stampInductorTrapezoidal(int nodeI, int nodeJ, double L, double h, 
                                      double i_prev, double v_prev, 
                                      MatrixConstructor& matrixBuilder, std::vector<double>& rhsVector)
{
    // R_eq = 2*L / h => G_eq = h / (2*L)
    double G_eq = h / (2.0 * L);
    double I_eq = i_prev + G_eq * v_prev;

    // Stamp conductance G_eq (like resistor)
    if (nodeI != 0) 
    {
        matrixBuilder.add(nodeI - 1, nodeI - 1, G_eq);
        if (nodeJ != 0) 
            matrixBuilder.add(nodeI - 1, nodeJ - 1, -G_eq);
    }
    if (nodeJ != 0) 
    {
        matrixBuilder.add(nodeJ - 1, nodeJ - 1, G_eq);
        if (nodeI != 0) 
            matrixBuilder.add(nodeJ - 1, nodeI - 1, -G_eq);
    }

    // Stamp equivalent current source
    if (nodeI != 0) 
        rhsVector[nodeI - 1] += I_eq;
    if (nodeJ != 0) 
        rhsVector[nodeJ - 1] -= I_eq;
}



inline void stampMosfet(int d, int g, int s, int b, double gm, double gds, MatrixConstructor& mat) 
{
    // Linearized Small-Signal Model Stamp
    // i_d += gm * (vg - vs) + gds * (vd - vs)
    // i_s -= gm * (vg - vs) - gds * (vd - vs)
    
    auto add = [&](int r, int c, double val) 
    {
        if (r != 0 && c != 0) 
            mat.add(r-1, c-1, val);
    };
    
    // GM Term (VCCS)
    add(d, g, gm); 
    add(d, s, -gm);
    add(s, g, -gm); 
    add(s, s, gm);
    
    // GDS Term (Resistor)
    add(d, d, gds); 
    add(d, s, -gds);
    add(s, d, -gds); 
    add(s, s, gds);
}

inline void stampDiode(int anode, int cathode, double g_d, double i_d, double v_d, 
                       MatrixConstructor& mat, std::vector<double>& rhs) {
    // Linearization: I = I_eq + G_eq * V
    // where I_eq = i_d - g_d * v_d
    double i_eq = i_d - g_d * v_d;
    
    // Stamp Conductance (G)
    // +Node gets +G, -Node gets +G, Mutual gets -G
    if (anode != 0) {
        mat.add(anode-1, anode-1, g_d);
        if (cathode != 0) mat.add(anode-1, cathode-1, -g_d);
        rhs[anode-1] -= i_eq; // Current LEAVING anode
    }
    if (cathode != 0) {
        mat.add(cathode-1, cathode-1, g_d);
        if (anode != 0) mat.add(cathode-1, anode-1, -g_d);
        rhs[cathode-1] += i_eq; // Current ENTERING cathode
    }
}

inline void stampBJT(int c, int b, int e, double g_cc, double g_cb, double g_ce,
                     double g_bc, double g_bb, double g_be,
                     double g_ec, double g_eb, double g_ee,
                     double ic, double ib, double ie,
                     double vc, double vb, double ve,
                     MatrixConstructor& mat, std::vector<double>& rhs) {
    
    // Stamps for 3-terminal device are 3x3 block
    // RHS = I_equiv = I_k - (G_k * V_k) ... but vector form
    
    // I_eq_c = ic - (g_cc*vc + g_cb*vb + g_ce*ve)
    // I_eq_b = ib - (g_bc*vc + g_bb*vb + g_be*ve)
    // I_eq_e = ie - (g_ec*vc + g_eb*vb + g_ee*ve)
    
    double i_eq_c = ic - (g_cc*vc + g_cb*vb + g_ce*ve);
    double i_eq_b = ib - (g_bc*vc + g_bb*vb + g_be*ve);
    double i_eq_e = ie - (g_ec*vc + g_eb*vb + g_ee*ve);
    
    auto addG = [&](int r, int c, double val) {
        if (r!=0 && c!=0) mat.add(r-1, c-1, val);
    };
    
    auto addRHS = [&](int r, double val) {
        if (r!=0) rhs[r-1] -= val; // RHS is -I_eq (current entering node)
    };
    
    // Collector Row
    addG(c, c, g_cc); addG(c, b, g_cb); addG(c, e, g_ce);
    addRHS(c, i_eq_c);
    
    // Base Row
    addG(b, c, g_bc); addG(b, b, g_bb); addG(b, e, g_be);
    addRHS(b, i_eq_b);
    
    // Emitter Row
    addG(e, c, g_ec); addG(e, b, g_eb); addG(e, e, g_ee);
    addRHS(e, i_eq_e);
}

/**
 * stampJFET - Linearized JFET Small-Signal Stamp
 * 
 * JFET has 3 terminals: Drain (D), Gate (G), Source (S)
 * 
 * Linearized model:
 *   i_d = gm * (vg - vs) + gds * (vd - vs)
 *   i_s = -i_d (assuming no gate current in Shichman-Hodges)
 *   i_g = 0 (ideal gate)
 * 
 * The stamp follows MOSFET small-signal pattern.
 */
inline void stampJFET(int d, int g, int s, double gm, double gds, double ids,
                       double vd, double vg, double vs,
                       MatrixConstructor& mat, std::vector<double>& rhs) {
    
    // GM Term (VCCS: i_d = gm * vgs)
    // Stamp pattern: i_d = gm * (v_g - v_s)
    auto addG = [&](int r, int c, double val) {
        if (r != 0 && c != 0) 
            mat.add(r-1, c-1, val);
    };
    
    // GDS Term (Output Conductance: i_d = gds * vds)
    addG(d, d, gds);  addG(d, s, -gds);
    addG(s, d, -gds); addG(s, s, gds);
    
    // GM Term (Transconductance)
    addG(d, g, gm);  addG(d, s, -gm);
    addG(s, g, -gm); addG(s, s, gm);
    
    // RHS: Equivalent current source
    // I_eq = Ids(Vk) - (gm * (Vg - Vs) + gds * (Vd - Vs))
    double vgs = vg - vs;
    double vds = vd - vs;
    double i_eq = ids - (gm * vgs + gds * vds);
    
    if (d != 0) rhs[d-1] -= i_eq; // Current leaving drain
    if (s != 0) rhs[s-1] += i_eq; // Current entering source
}
