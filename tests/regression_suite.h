#pragma once
#include "circuitsim.h"
#include "regression_db.h"
#include <functional>
#include <vector>
#include <string>
#include <iostream>

using TestCase = std::function<SolverStep()>;

struct RegisteredTest {
    std::string name;
    TestCase testFunc;
};

class RegressionSuite {
private:
    std::vector<RegisteredTest> tests;
    RegressionDB db;

public:
    RegressionSuite() : db("regression_golden.db") {}

    void registerTest(const std::string& name, TestCase test) {
        tests.push_back({name, test});
    }

    void run(bool updateGolden = false) {
        std::cout << "==================================================\n";
        std::cout << " AUTOMATED REGRESSION SUITE\n";
        std::cout << " Tests: " << tests.size() << "\n";
        std::cout << " Mode: " << (updateGolden ? "UPDATE GOLDEN" : "VERIFY") << "\n";
        std::cout << "==================================================\n";

        int passed = 0;
        int failed = 0;

        for (const auto& t : tests) {
             std::cout << "Running " << t.name << "... ";
             
             try {
                SolverStep result = t.testFunc();
                
                if (updateGolden) {
                    db.update(t.name, result.nodeVoltages, result.stats.residual, result.stats.iterations, result.stats.converged);
                    std::cout << "[UPDATED] (conv=" << (result.stats.converged ? "Y" : "N") 
                              << ", iter=" << result.stats.iterations 
                              << ", res=" << result.stats.residual << ")\n";
                    passed++;
                } else {
                    bool ok = db.compare(t.name, result.nodeVoltages);
                    if (ok) {
                        std::cout << "[PASS] (conv=" << (result.stats.converged ? "Y" : "N") 
                                  << ", iter=" << result.stats.iterations 
                                  << ", res=" << result.stats.residual << ")\n";
                        passed++;
                    } else {
                        failed++;
                    }
                }

             } catch (const std::exception& e) {
                 std::cout << "[CRASH] " << e.what() << "\n";
                 failed++;
             } catch (...) {
                 std::cout << "[CRASH] Unknown error\n";
                 failed++;
             }
        }

        if (updateGolden) db.save();

        std::cout << "==================================================\n";
        std::cout << " SUMMARY: " << passed << " Passed, " << failed << " Failed\n";
        std::cout << "==================================================\n";
    }
};

// ===================================
// STANDARD BENCHMARKS
// ===================================
namespace Benchmarks {

SolverStep linearDivider() {
    TensorNetlist nl;
    nl.addVoltageSource(1, 0, 10.0);
    nl.addResistor(1, 2, 2000.0); // 2k
    nl.addResistor(2, 0, 2000.0); // 2k
    
    CircuitSim sim;
    return sim.solveDC(nl);
}

SolverStep diodeCircuit() {
    TensorNetlist nl;
    nl.addCurrentSource(0, 1, 1.0); // 1A source
    nl.addDiode(1, 0, 1e-14);       // Standard diode
    
    CircuitSim sim;
    return sim.solveDC(nl);
}

SolverStep bridgeRectifier() {
    // Full bridge rectifier (DC snapshot)
    TensorNetlist nl;
    nl.addVoltageSource(1, 2, 10.0); // AC source snapshot 10V
    // 4 Diodes
    nl.addDiode(1, 3, 1e-14); // D1
    nl.addDiode(0, 1, 1e-14); // D2
    nl.addDiode(2, 3, 1e-14); // D3
    nl.addDiode(0, 2, 1e-14); // D4
    nl.addResistor(3, 0, 100.0); // Load
    
    // Anchor both ends of the source to ground via high impedance to avoid floating nodes
    nl.addResistor(1, 0, 1e6); 
    nl.addResistor(2, 0, 1e6); 
    
    CircuitSim sim;
    return sim.solveDC(nl);
}

SolverStep darlingtonPair() {
    TensorNetlist nl;
    // Supply: Node 1 (10V)
    nl.addVoltageSource(1, 0, 10.0);
    
    // Load: R1 (1 ohm) between VCC and Collector
    nl.addResistor(1, 2, 1.0);
    
    // Transistors: Q1, Q2 in Darlington configuration
    // Q1: Collector=2, Base=3, Emitter=4
    nl.addBJT("Q1", 2, 3, 4, "genericNPN", true, 100.0, 1.0, 1e-14);
    // Q2: Collector=2, Base=4, Emitter=0
    nl.addBJT("Q2", 2, 4, 0, "genericNPN", true, 100.0, 1.0, 1e-14);
    
    // Biasing: Current Source (10uA) into Q1 Base
    nl.addCurrentSource(1, 3, 1e-5);
    
    CircuitSim sim;
    return sim.solveDC(nl);
}

} // namespace Benchmarks
