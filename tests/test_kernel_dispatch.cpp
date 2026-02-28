#include <iostream>
#include <vector>
#include <cmath>
#include <cassert>
#include "acutesim_engine/tensors/kernel_dispatch.h"
#include "acutesim_engine/tensors/physics_tensors.h"

using namespace acutesim::compute;

void testDiodeDispatch() {
    std::cout << "Testing Diode Dispatch..." << std::endl;

    DiodeTensor diodes;
    // Anode=1, Cathode=0 (GND)
    // Is=1e-12, N=1.0, Vt=0.025
    diodes.push(1, 0, 1e-12, 1.0, 0.025);

    // Global Voltages: Node 1 = 0.7V
    std::vector<double> voltages = {0.8}; 

    // Run Kernel via Dispatcher Wrapper
    // (This calls batchDiodePhysics -> KernelDispatcher -> generic/avx impl)
    batchDiodePhysics(diodes, voltages);

    // Check Result (Manual Clamping Calculation to match Kernel)
    double expected_v = 0.8;
    // Kernel clamps arg at 30.0 and adds linear continuation
    double n_vt = 1.0 * 0.025;
    double arg = expected_v / n_vt;
    double arg_clamped = (arg > 30.0) ? 30.0 : arg;
    
    double is = 1e-12;
    double exp_val = std::exp(arg_clamped);
    
    double i_base = is * (exp_val - 1.0);
    double g_base = (is / n_vt) * exp_val;
    
    double delta = (expected_v > (30.0 * n_vt)) ? (expected_v - 30.0 * n_vt) : 0.0;
    double expected_i_clamped = i_base + delta * g_base;
    
    std::cout << "Vd: " << diodes.v_d[0] << " (Expected: " << expected_v << ")" << std::endl;
    std::cout << "Id: " << diodes.i_d[0] << " (Expected Clamped: " << expected_i_clamped << ")" << std::endl;

    // Tolerance check
    if (std::abs(diodes.i_d[0] - expected_i_clamped) < 1e-9) {
        std::cout << "PASS: Diode Physics matches analytical expectation." << std::endl;
    } else {
        std::cerr << "FAIL: Diode Physics mismatch!" << std::endl;
        exit(1);
    }
}

int main() {
    auto& dispatcher = KernelDispatcher::get();
    std::cout << "Dispatcher initialized. Architecture detected: " << (int)dispatcher.architecture << std::endl;

    testDiodeDispatch();
    
    std::cout << "All Dispatch Tests Passed." << std::endl;
    return 0;
}
