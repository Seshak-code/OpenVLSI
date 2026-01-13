#include "../physics/circuitsim.h"
#include "../physics/superposition.h"
#include <iostream>
#include <cassert>
#include <cmath>

bool isClose(double a, double b, double tol = 1e-4) {
    return std::abs(a - b) < tol;
}

int main() {
    std::cout << "===========================================" << std::endl;
    std::cout << " TEST: Superposition Principle Verification" << std::endl;
    std::cout << "===========================================" << std::endl;

    // Setup Circuit: V1 (10V) -- R1 (1k) -- Node 2 -- R2 (1k) -- Gnd
    //                 +--> I2 (1mA into Node 2)
    
    TensorNetlist netlist;
    netlist.numGlobalNodes = 2; // Node 1 (V1), Node 2 (Output)
    
    // V1: 10V at Node 1
    VoltageSource v1;
    v1.nodePositive = 1;
    v1.nodeNegative = 0;
    v1.voltage_V = 10.0;
    netlist.globalBlock.voltageSources.push_back(v1);
    
    // R1: 1k from 1 to 2
    Resistor r1;
    r1.nodeTerminal1 = 1;
    r1.nodeTerminal2 = 2;
    r1.resistance_ohms = 1000.0;
    netlist.globalBlock.resistors.push_back(r1);
    
    // R2: 1k from 2 to 0
    Resistor r2;
    r2.nodeTerminal1 = 2;
    r2.nodeTerminal2 = 0;
    r2.resistance_ohms = 1000.0;
    netlist.globalBlock.resistors.push_back(r2);
    
    // I2: 1mA into Node 2
    // Stamping: rhsVector[nP] -= I, rhsVector[nN] += I.
    // If nN=2, current enters Node 2.
    CurrentSource i2;
    i2.nodePositive = 0;
    i2.nodeNegative = 2;
    i2.current_A = 0.001; // 1mA
    netlist.globalBlock.currentSources.push_back(i2);
    
    CircuitSim sim;

    // 1. Standard Solve
    std::cout << "Running Standard Solve..." << std::endl;
    auto res = sim.solveDC(netlist);
    if (res.nodeVoltages.size() < 2) {
        std::cerr << "[FAIL] Solver returned empty/short solution." << std::endl;
        return 1;
    }
    double v2_std = res.nodeVoltages[1]; // Node 2 (0-indexed 1)
    std::cout << "  Standard V2: " << v2_std << " V" << std::endl;
    
    // 2. Superposition
    std::cout << "Running Superposition Decomposition..." << std::endl;
    auto subproblems = SuperpositionSolver::decompose(netlist);
    std::cout << "  Decomposed into " << subproblems.size() << " sub-problems." << std::endl;
    
    if (subproblems.size() != 2) {
         std::cerr << "[FAIL] Expected 2 sub-problems (V1, I2), got " << subproblems.size() << std::endl;
         return 1;
    }
    
    for (auto& prob : subproblems) {
        // Important: Create new sim or reset to ensure no state leak (though solveDC is mostly stateless for DC)
        // Check prob.netlist source values
        double v = prob.netlist.globalBlock.voltageSources[0].voltage_V;
        double i = prob.netlist.globalBlock.currentSources[0].current_A;
        std::cout << "  solving sub (" << prob.sourceName << "): V1=" << v << ", I2=" << i << " ... ";
        
        auto subRes = sim.solveDC(prob.netlist);
        prob.solution = subRes.nodeVoltages;
        double v2_sub = (!prob.solution.empty()) ? prob.solution[1] : 0.0;
        std::cout << "V2=" << v2_sub << " V" << std::endl;
    }
    
    auto v_total = SuperpositionSolver::combine(subproblems);
    double v2_sup = (!v_total.empty()) ? v_total[1] : 0.0;
    std::cout << "  Superposition Total V2: " << v2_sup << " V" << std::endl;
    
    double diff = std::abs(v2_sup - v2_std);
    std::cout << "  Difference: " << diff << " V" << std::endl;

    if (isClose(v2_std, v2_sup)) {
        std::cout << "[PASS] Superposition matches standard solve." << std::endl;
        return 0;
    } else {
        std::cout << "[FAIL] Mismatch! Expected " << v2_std << ", got " << v2_sup << std::endl;
        return 1;
    }
}
