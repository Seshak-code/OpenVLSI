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

inline void stampVoltageSourceAsNorton(int nodePos, int nodeNeg, double voltage, 
                                        MatrixConstructor& matrixBuilder, std::vector<double>& rhsVector)
{
    // We treat independent voltage sources as high-conductance Norton equivalents 
    // to keep the MNA matrix entirely in the voltage domain.
    double r_int = 1e-3;
    double g_int = 1.0 / r_int;
    double i_eq = voltage * g_int;

    stampResistor(nodePos, nodeNeg, r_int, matrixBuilder);

    if (nodePos != 0) 
        rhsVector[nodePos - 1] += i_eq;
    if (nodeNeg != 0) 
        rhsVector[nodeNeg - 1] -= i_eq;
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
