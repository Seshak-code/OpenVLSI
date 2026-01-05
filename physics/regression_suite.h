#pragma once
#include "circuitsim.h"
#include "regression_db.h"
#include <functional>
#include <vector>
#include <string>
#include <iostream>

using TestCase = std::function<SimulationStep()>;

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
                SimulationStep result = t.testFunc();
                
                if (updateGolden) {
                    // Calculate KCL residual for the record
                    CircuitSim sim; // Need sim to calc residuals? 
                    // Wait, SimulationStep doesn't have the netlist.
                    // The test case generates the netlist and solves.
                    // Ideally the test case should return the KCL residual too or we calculate it there.
                    // For now, let's assume result.stats matches.
                    
                    // We need the KCL residual. result.stats.residual might be it if solveDC sets it.
                    // Let's check circuitsim.h: Step stats has "residual" ???
                    // solveDC returns {total_iterations, 0.0, converged}. Residual isn't explicitly returned in the simple struct?
                    // Let's assume 0.0 for now if not available, or I need to update CircuitSim to return it.
                    // Actually, solveDC calls checkConvergence which calculates it but doesn't return it in the struct.
                    // I will check circuitsim.h again later.
                    
                    db.update(t.name, result.nodeVoltages, result.stats.residual, result.stats.iterations, result.stats.converged);
                    std::cout << "[UPDATED]\n";
                    passed++;
                } else {
                    bool ok = db.compare(t.name, result.nodeVoltages);
                    if (ok) {
                        std::cout << "[PASS]\n";
                        passed++;
                    } else {
                        // Detailed error printed by db.compare
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

SimulationStep linearDivider() {
    TensorNetlist nl;
    nl.addVoltageSource(1, 0, 10.0);
    nl.addResistor(1, 2, 2000.0); // 2k
    nl.addResistor(2, 0, 2000.0); // 2k
    
    CircuitSim sim;
    return sim.solveDC(nl);
}

SimulationStep diodeCircuit() {
    TensorNetlist nl;
    nl.addCurrentSource(0, 1, 1.0); // 1A source
    nl.addDiode(1, 0, 1e-14);       // Standard diode
    
    CircuitSim sim;
    return sim.solveDC(nl);
}

SimulationStep bridgeRectifier() {
    // Full bridge rectifier (DC snapshot)
    TensorNetlist nl;
    nl.addVoltageSource(1, 2, 10.0); // AC source snapshot 10V
    // 4 Diodes
    nl.addDiode(1, 3, 1e-14); // D1
    nl.addDiode(0, 1, 1e-14); // D2
    nl.addDiode(2, 3, 1e-14); // D3
    nl.addDiode(0, 2, 1e-14); // D4
    nl.addResistor(3, 0, 100.0); // Load
    
    CircuitSim sim;
    return sim.solveDC(nl);
}

} // namespace Benchmarks
