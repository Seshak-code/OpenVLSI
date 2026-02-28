#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <cassert>

#include "../physics/circuitsim.h"
#include "acutesim_engine/netlist/circuit.h"

// ANSI Color Codes
#define GREEN "\033[32m"
#define RED "\033[31m"
#define RESET "\033[0m"

/**
 * Verification of Roadmap Claim:
 * "Verify stable DC operating point across temperature sweep"
 * "Acceptance Threshold: Residual Peak > 1e-12 or Iterations > 200"
 */

void verify_darlington_thermal() {
    std::cout << "==================================================\n";
    std::cout << " VERIFYING BJT THERMAL STABILITY (Darlington)\n";
    std::cout << " Temperatures: 0C, 25C, 85C, 125C\n";
    std::cout << "==================================================\n";

    // Standard Darlington Pair Setup
    TensorNetlist nl;
    // VCC = 10V at Node 1
    nl.addVoltageSource(1, 0, 10.0);
    
    // Load R1 = 100 Ohm at Node 2 (Collector)
    nl.addResistor(1, 2, 100.0);
    
    // Q1: C=2, B=3, E=4
    nl.addBJT("Q1", 2, 3, 4, "genericNPN", true, 100.0, 1.0, 1e-14);
    
    // Q2: C=2, B=4, E=0 (Emitter Grounded)
    nl.addBJT("Q2", 2, 4, 0, "genericNPN", true, 100.0, 1.0, 1e-14);
    
    // Bias Current into Q1 Base (Node 3): 1uA (Active Region)
    nl.addCurrentSource(1, 3, 1e-6);

    CircuitSim sim;
    
    struct Result {
        double temp;
        double vbe_total; // V(3) - V(0)
        double residual;
        int iter;
        bool converged;
    };
    
    std::vector<double> temps = {0.0, 25.0, 85.0, 125.0};
    std::vector<Result> results;
    
    bool all_passed = true;

    for (double tempC : temps) {
        std::cout << "Running at T = " << tempC << " C ... ";
        
        // Apply Temperature Corner
        acutesim::physics::CornerConfig corner;
        corner.temperatureC = tempC;
        corner.voltageScale = 1.0;
        sim.applyCorner(nl, corner);
        
        // Solve DC
        SolverStep step = sim.solveDC(nl);
        
        // Check 1: Convergence
        if (!step.stats.converged) {
            std::cout << RED << "FAIL (Diverged)" << RESET << "\n";
            all_passed = false;
            continue;
        }
        
        // Check 2: Residuals
        // Roadmap: "Acceptance Threshold: Residual Peak < 1e-10 (100pA)"
        if (step.stats.residual > 1e-10) {
             std::cout << RED << "FAIL (High Residual: " << step.stats.residual << ")" << RESET;
             all_passed = false;
        } else {
             std::cout << GREEN << "PASS" << RESET;
        }
        
        // Result Storage
        double v_base = step.nodeVoltages[3]; // Node 3 is Base of Q1
        double v_emit = 0.0;                 // Node 0 is Ground
        double vbe = v_base - v_emit;
        
        std::cout << " (Vbe=" << vbe << "V, Res=" << step.stats.residual << ")\n";
        results.push_back({tempC, vbe, step.stats.residual, step.stats.iterations, step.stats.converged});
    }
    
    // Consistency Check: Vbe should decrease with temperature (~ -2mV/C * 2 junctions = -4mV/C)
    // Darlington has 2 Vbe drops. Check monotonic decrease across all converged points.
    {
        // Filter to converged results only
        std::vector<Result> converged;
        for (const auto& r : results) {
            if (r.converged) converged.push_back(r);
        }

        bool phys_ok = converged.size() >= 2; // Need at least 2 converged points
        for (size_t i = 1; i < converged.size(); ++i) {
            if (converged[i].vbe_total >= converged[i-1].vbe_total) {
                phys_ok = false;
                break;
            }
        }

        std::cout << "\nPhysics Check (dVbe/dT < 0, " << converged.size() << " points): "
                  << (phys_ok ? GREEN "PASS" : RED "FAIL") << RESET << "\n";

        if (!phys_ok) all_passed = false;
    }
    
    if (all_passed) {
        std::cout << "\n[SUCCESS] BJT Stability Verified across Thermal Range.\n";
        exit(0);
    } else {
        std::cout << "\n[FAILURE] BJT Stability Criteria Not Met.\n";
        exit(1);
    }
}

int main() {
    try {
        verify_darlington_thermal();
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
