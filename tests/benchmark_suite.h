#pragma once
#include "circuitsim.h"
#include <iostream>
#include <iomanip>
#include <cmath>

class GoldBench
{
public:
    static void run()
    {
        std::cout << "==================================================\n";
        std::cout << " SPICE GOLDEN BENCHMARK SUITE\n";
        std::cout << " Comparing against theoretical/HSPICE targets.\n";
        std::cout << "==================================================\n";

        runResistiveDivider();
        runDiodeIV();
        // runNMOS() - Placeholder for future expansion
        
        std::cout << "==================================================\n";
    }

private:
    static void report(const std::string& testName, double measured, double expected, double tol)
    {
        double error = std::abs(measured - expected);
        double errorPercent = (expected != 0.0) ? (error / std::abs(expected)) * 100.0 : error;
        bool pass = errorPercent < tol;

        std::cout << " [TEST] " << std::left << std::setw(25) << testName 
                  << " | " << (pass ? "PASS" : "FAIL") 
                  << " | Meas: " << std::fixed << std::setprecision(6) << measured
                  << " | Ref: " << expected 
                  << " | Err: " << std::scientific << std::setprecision(2) << errorPercent << "%\n";
    }

    static void runResistiveDivider()
    {
        // 1. Simple Divider: 10V -> 2k / 2k -> Should be 5.0V exactly
        TensorNetlist netlist;
        netlist.addVoltageSource(1, 0, 10.0);
        netlist.addResistor(1, 2, 2000.0);
        netlist.addResistor(2, 0, 2000.0);

        CircuitSim sim;
        SolverStep res = sim.solveDC(netlist);
        
        double v_out = (res.nodeVoltages.size() >= 2) ? res.nodeVoltages[1] : 0.0; // Node 2 is index 1
        report("Ohm's Law (Res Divider)", v_out, 5.000000, 0.01); // 0.01% tolerance
    }

    static void runDiodeIV()
    {
        // 2. Diode Voltage Drop: 1A through a diode
        // I = Is * (exp(V/Vt) - 1)  =>  V = Vt * ln(I/Is + 1)
        // With Is=1e-14, N=1, Vt=0.02585, I=1A
        // Expected V ≈ 0.02585 * ln(1e14 + 1) ≈ 0.02585 * 32.236 ≈ 0.8333V
        
        double Is = 1e-14;
        double Vt = 0.02585;
        double targetCurrent = 1.0;
        double expectedV = Vt * std::log(targetCurrent/Is + 1.0);

        TensorNetlist netlist;
        // Source pushes current INTO Node 1. 
        // Logic: P=0, N=1. Current flows 0->1 through source. Enters 1.
        netlist.addCurrentSource(0, 1, targetCurrent); 
        netlist.addDiode(1, 0, Is);

        CircuitSim sim;
        SolverStep res = sim.solveDC(netlist);

        double v_out = (res.nodeVoltages.size() >= 1) ? res.nodeVoltages[0] : 0.0; // Node 1 is index 0
        report("Shockley Diode V_drop", v_out, expectedV, 0.1); 
    }
};
