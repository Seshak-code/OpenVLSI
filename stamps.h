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
