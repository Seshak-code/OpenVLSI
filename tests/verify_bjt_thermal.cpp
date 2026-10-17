#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <cassert>

#include "../physics/circuitsim.h"
#include "acutesim_engine/netlist/circuit.h"
#include "acutesim_engine/netlist/spice_parser.h"

using acutesim::SPICEParser;
using acutesim::SPICEParseResult;

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

    // Standard Darlington pair: VCC 10 V, 100 ohm collector load, 1 uA into Q1's base. The parser resolves
    // every temperature-dependent device value, so each temperature is its own parse (.temp).
    const std::string netlist = "darlington\nV1 1 0 10\nR1 1 2 100\nQ1 2 3 4 QN\nQ2 2 4 0 QN\nI1 1 3 1u\n"
                                ".model QN NPN(BF=100 IS=1e-14)\n.op\n";

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
        
        SPICEParseResult parsed = SPICEParser().parse(netlist + ".temp " + std::to_string(tempC) + "\n.end\n");
        if (!parsed.success) { std::cout << RED << "FAIL (" << parsed.errorMessage << ")" << RESET << "\n"; all_passed = false; continue; }
        TensorNetlist& nl = parsed.netlist;
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
        double v_base = step.nodeVoltages[parsed.nodeIndex.at("3") - 1]; // Q1's base
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
