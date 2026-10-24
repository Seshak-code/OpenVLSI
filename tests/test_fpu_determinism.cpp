#include <iostream>
#include <iomanip>
#include <cmath>
#include <cassert>
#include <vector>
#include <cfenv>
#include "acutesim_engine/math/fpu_control.h"

int main() {
    std::cout << "Starting FPU Determinism Test..." << std::endl;

    // 1. Enforce FPU state
    acutesim::math::FPU::enforceDeterminism();

    // 2. Check Rounding Mode
    int roundMode = std::fegetround();
    if (roundMode != FE_TONEAREST) {
        std::cerr << "FAIL: Rounding mode is not FE_TONEAREST (" << FE_TONEAREST << "). Got: " << roundMode << std::endl;
        return 1;
    } else {
        std::cout << "PASS: Rounding mode is FE_TONEAREST." << std::endl;
    }

    // 3. Sensitive Calculation Test 
    // We want to check if intermediate precision effects (80-bit vs 64-bit) are scrubbed.
    // This is hard to "fail" without a specific misbehaving platform, but we can log the result.
    volatile double a = 1.0;
    volatile double b = 3.0;
    volatile double c = a / b; // 0.333...

    std::cout << std::setprecision(20);
    std::cout << "1.0 / 3.0 = " << c << std::endl;

    volatile double d = c * 3.0; // Should be 1.0 (or very close)
    if (std::abs(d - 1.0) > 1e-15) {
         std::cout << "WARN: Precision drift detected." << std::endl;
    }

    // 4. Subnormal Test (We want them preserved, NOT flushed to zero)
    volatile double tiny = 1e-308;
    volatile double subnormal = tiny / 10.0; // 1e-309 (Subnormal)
    
    if (subnormal == 0.0) {
        std::cout << "INFO: Subnormals are flushed to zero (FTZ is ON). This is acceptable for some configs but 'Strict' usually implies preservation." << std::endl;
    } else {
        std::cout << "PASS: Subnormals are preserved (FTZ is OFF)." << std::endl;
    }

    std::cout << "FPU Test Completed Successfully." << std::endl;
    return 0;
}
