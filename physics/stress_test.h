#pragma once
#include "circuitsim.h"
#include <random>
#include <iostream>
#include <iomanip>
#include <chrono>

/**
 * CircuitFuzzer
 * Generates randomized, potentially pathological circuit topologies.
 */
class CircuitFuzzer 
{
public:
    static TensorNetlist generateRandom(int numNodes, int numComponents, uint32_t seed) 
    {
        TensorNetlist netlist;
        std::mt19937 gen(seed);
        
        // Bias: Higher chance of connecting to ground (0) to ensure circuit is referenced
        std::uniform_int_distribution<int> nodeDist(0, numNodes); 
        auto getBiasNode = [&]() {
            if (std::uniform_real_distribution<double>(0.0, 1.0)(gen) < 0.3) return 0;
            return nodeDist(gen);
        };

        std::uniform_real_distribution<double> valDist(1.0, 1e4); // Slightly more reasonable range
        std::uniform_int_distribution<int> typeDist(0, 3); // R, C, D, V

        for (int i = 0; i < numComponents; ++i) 
        {
            int n1 = getBiasNode();
            int n2 = getBiasNode();
            if (n1 == n2) continue;

            int type = typeDist(gen);
            double val = valDist(gen);

            switch (type) 
            {
                case 0: // Resistor
                    netlist.addResistor(n1, n2, val);
                    break;
                case 1: // Capacitor
                    netlist.addCapacitor(n1, n2, val * 1e-12);
                    break;
                case 2: // Diode
                    netlist.addDiode(n1, n2, 1e-14);
                    break;
                case 3: // Voltage Source
                    netlist.addVoltageSource(n1, n2, val / 1000.0);
                    break;
            }
        }
        return netlist;
    }
};

/**
 * StressHarness
 * Runs the fuzzer and reports on solver robustness.
 */
class StressHarness 
{
public:
    struct Stats 
    {
        int totalTests = 0;
        int converged = 0;
        int failedNR = 0;
        int matrixErrors = 0;
        double avgKclResidual = 0.0;
        double maxKclResidual = 0.0;
    };

    static void run(int numTrials = 100) 
    {
        Stats stats;
        CircuitSim sim;
        stats.totalTests = numTrials;

        std::cout << "==================================================\n";
        std::cout << " RESEARCH-GRADE SOLVER STRESS TEST\n";
        std::cout << " Running " << numTrials << " randomized trials...\n";
        std::cout << "==================================================\n";

        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < numTrials; ++i) 
        {
            TensorNetlist netlist = CircuitFuzzer::generateRandom(10, 25, 42 + i);
            
            try 
            {
                SimulationStep result = sim.solveDC(netlist);
                
                if (result.stats.converged) 
                {
                    stats.converged++;
                    double kcl = sim.calculatePhysicalResiduals(netlist, result.nodeVoltages);
                    stats.avgKclResidual += kcl;
                    if (kcl > stats.maxKclResidual) stats.maxKclResidual = kcl;
                } 
                else 
                {
                    stats.failedNR++;
                }
            } 
            catch (...) 
            {
                stats.matrixErrors++;
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end - start;

        if (stats.converged > 0) stats.avgKclResidual /= stats.converged;

        std::cout << std::fixed << std::setprecision(4);
        std::cout << "RESULTS:\n";
        std::cout << "  - Total Trials:     " << stats.totalTests << "\n";
        std::cout << "  - Converged:        " << stats.converged << " (" << (100.0 * stats.converged / numTrials) << "%)\n";
        std::cout << "  - NR Failures:      " << stats.failedNR << "\n";
        std::cout << "  - Matrix Crashes:   " << stats.matrixErrors << "\n";
        std::cout << std::scientific << std::setprecision(2);
        std::cout << "  - Avg KCL Error:    " << stats.avgKclResidual << " A\n";
        std::cout << "  - Max KCL Error:    " << stats.maxKclResidual << " A\n";
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "  - Time Elapsed:     " << elapsed.count() << "s\n";
        std::cout << "==================================================\n";
    }
};
