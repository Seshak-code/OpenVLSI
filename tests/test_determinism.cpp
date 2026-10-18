#include <cstdint>
#include <iostream>
#include <vector>
#include <cmath>
#include <iomanip>
#include <cassert>
#include <algorithm>
#include "physics/circuitsim.h"
#include "components/standard_cells.h"

// Determinism Replay Test
// Objective: Run the same randomized netlist twice on the SAME machine/binary and assert bit-exact identity.
// Failure implies: Uninitialized memory, race conditions (if threaded), or non-deterministic OS scheduling effects on math (unlikely but possible with x87).

using namespace StandardCells;

struct SimulationResult {
    std::vector<double> timepoints;
    std::vector<std::vector<double>> nodeVoltages; // [step][node]
};

SimulationResult run_simulation() {
    TensorNetlist nl;
    nl.environment.global_voltage_scale = 1.0;
    
    // Create a chain of inverters effectively acts as a ring oscillator if looped, 
    // or just a delay line. We want sensitivity to initial conditions.
    
    // 5-stage Ring Oscillator
    int N = 5;
    for(int i=0; i<N; ++i) {
        // Nodes: 1..N.
        // Stage i inv: Input i, Output (i+1)%N ?
        // 1 -> 2 -> 3 -> 4 -> 5 -> 1
        
        // DFF or just Inverter? Use Inverters for analog chaos/oscillations.
        // Node 0 is GND.
        // Nodes 1..N are signal nodes.
        // Node N+1 is VDD.
        
        int in = i + 1;
        int out = (i + 1) % N + 1;
        
        // Add Inverter manually to avoid standard cell complex lookup overhead for this pure determinism test?
        // Actually, let's use mosfets directly to test the solver core.
        
        // CMOS Inverter
        // NFET: D=out, G=in, S=0
        // PFET: D=out, G=in, S=VDD
        
        nl.addMosfet("M_N_" + std::to_string(i), out, in, 0, 0, "nfet", 1e-6, 180e-9);
        nl.addMosfet("M_P_" + std::to_string(i), out, in, N+1, N+1, "pfet", 2e-6, 180e-9); 
    }
    
    // Power
    nl.addVoltageSource(N+1, 0, 1.8);
    
    // Initial condition kick (force node 1 to 0V initially)
    // Actually, solver starts at 0. Ring oscillator needs noise or kick start.
    // We add a pulse source to Node 1.
    nl.addVoltageSource(1, 0, 0.0, "PULSE(0 1.8 0 10p 10p 5n 10n)"); // Fake string parsing or manual? 
    // Manual Pulse:
    // We will apply it in the loop.
    
    CircuitSim sim;
    sim.setIntegrationMethod(IntegrationType::TRAPEZOIDAL);
    
    double t = 0.0;
    double dt = 10e-12; // 10ps
    double t_end = 10e-9; // 10ns
    
    SimulationResult res;
    
    // Solve
    while(t < t_end) {
        // Kick start logic
        if (t < 100e-12) {
             // Force Node 1 to 0 via a low-resistance source? 
             // Or just set initial guess.
             // Let's just run. The 0 state is unstable for 5-stage?
             // Actually, 0->1->0->1->0->1 (conflict). 
             // Let's force Node 1 to 0 for first step.
        }
        
        // We rely on the solver's internal convergence.
        // To ensure activity, let's inject a current pulse into Node 1
        if (t > 1e-9 && t < 1.1e-9) {
            nl.addCurrentSource(1, 0, 100e-6); // 100uA pulse
        } else {
            // Remove? The struct is simple, `addCurrentSource` adds to list. 
            // We should clear and rebuild or specific update.
            // For determinism test, let's just use static netlist modification if supported or just purely static.
            // Static netlist with transient sources is better.
        }

        SolverStep step = sim.stepTransient(nl, dt, t);
        
        res.timepoints.push_back(t);
        res.nodeVoltages.push_back(step.nodeVoltages);
        
        t += dt;
    }
    
    return res;
}

int main() {
    std::cout << "Starting Determinism Replay Test...\n";
    
    std::cout << "Run 1...\n";
    SimulationResult r1 = run_simulation();
    
    std::cout << "Run 2...\n";
    SimulationResult r2 = run_simulation();
    
    // Compare
    if (r1.timepoints.size() != r2.timepoints.size()) {
        std::cout << "FAIL: Step count mismatch! Run1: " << r1.timepoints.size() 
                  << " Run2: " << r2.timepoints.size() << "\n";
        return 1;
    }
    
    bool mismatch = false;
    for(size_t i=0; i<r1.timepoints.size(); ++i) {
        if (r1.nodeVoltages[i].size() != r2.nodeVoltages[i].size()) {
             std::cout << "FAIL: Node count mismatch at step " << i << "\n";
             return 1;
        }
        for(size_t n=0; n<r1.nodeVoltages[i].size(); ++n) {
            double v1 = r1.nodeVoltages[i][n];
            double v2 = r2.nodeVoltages[i][n];
            
            // BIT EXACT CHECK
            // We interpret the double bits.
            uint64_t b1 = *reinterpret_cast<uint64_t*>(&v1);
            uint64_t b2 = *reinterpret_cast<uint64_t*>(&v2);
            
            if (b1 != b2) {
                std::cout << std::fixed << std::setprecision(20);
                std::cout << "FAIL: Mismatch at T=" << r1.timepoints[i] 
                          << " Node=" << n << "\n"
                          << "  Run1: " << v1 << " (" << std::hex << b1 << ")\n"
                          << "  Run2: " << v2 << " (" << std::hex << b2 << ")\n";
                mismatch = true;
                // Don't spam
                return 1; 
            }
        }
    }
    
    std::cout << "PASS: 2x Runs are Bit-Exact Identical.\n";
    return 0;
}
