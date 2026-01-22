#include <iostream>
#include <vector>
#include <cmath>
#include <cassert>
#include "../physics/sensitivity_analysis.h"

// Mock simulator for a Voltage Divider: Vout = Vin * R2 / (R1 + R2)
double simulateDivider(TensorBlock& block) {
    double Vin = 10.0;
    double R1 = 0.0;
    double R2 = 0.0;
    
    for (const auto& r : block.resistors) {
        if (r.name == "R1") R1 = r.resistance_ohms;
        if (r.name == "R2") R2 = r.resistance_ohms;
    }
    
    if (R1 + R2 == 0) return 0.0;
    return Vin * (R2 / (R1 + R2));
}

int main() {
    std::cout << "[TEST] Verifying Sensitivity Engine..." << std::endl;
    
    // Setup Nominal Circuit: Divider with R1=1k, R2=1k
    TensorBlock block;
    
    Resistor r1; r1.name = "R1"; r1.resistance_ohms = 1000.0;
    Resistor r2; r2.name = "R2"; r2.resistance_ohms = 1000.0;
    
    block.resistors.push_back(r1);
    block.resistors.push_back(r2);
    
    // Theoretical Sensitivity:
    // Vout = 10 * 1000 / 2000 = 5.0 V
    // dV/dR1 = 10 * R2 * (-1) / (R1+R2)^2 = -10 * 1000 / 4000000 = -0.0025
    // Norm S_R1 = -0.0025 * (1000 / 5.0) = -0.5
    
    // dV/dR2 = 10 * [ (R1+R2) - R2 ] / (R1+R2)^2 = 10 * R1 / (R1+R2)^2 = 0.0025
    // Norm S_R2 = 0.0025 * (1000 / 5.0) = 0.5
    
    std::cout << "  -> Running Sensitivity Analysis on Voltage Divider (R1=R2=1k)..." << std::endl;
    auto results = Analysis::SensitivityEngine::analyze(block, simulateDivider, 0.1); // 0.1% perturbation
    
    bool r1Found = false;
    bool r2Found = false;
    
    for (const auto& res : results) {
        std::cout << "     Component: " << res.componentName 
                  << " S=" << res.sensitivity 
                  << " NormS=" << res.normSensitivity << std::endl;
                  
        if (res.componentName == "R1") {
            r1Found = true;
            assert(std::abs(res.normSensitivity - (-0.5)) < 0.01);
        }
        if (res.componentName == "R2") {
            r2Found = true;
            assert(std::abs(res.normSensitivity - 0.5) < 0.01);
        }
    }
    
    assert(r1Found && r2Found);
    std::cout << "PASS" << std::endl;
    
    std::cout << "[TEST] Sensitivity Engine Verified Successfully!" << std::endl;
    return 0;
}
