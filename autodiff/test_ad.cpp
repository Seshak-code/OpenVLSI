#include <iostream>
#include <cassert>
#include <cmath>
#include "dual.h"

// NASA Rule #5: Check assertions
#define ASSERT_NEAR(a, b, eps) \
    if (std::abs((a) - (b)) > (eps)) { \
        std::cerr << "FAIL: " << (a) << " != " << (b) << std::endl; \
        exit(1); \
    }

template <typename T>
T simple_poly(T x) {
    // f(x) = x^2 + 2x + 1
    return x * x + 2.0 * x + 1.0;
}

template <typename T>
T transcendental(T x) {
    // f(x) = exp(x) * sin(x)
    // f'(x) = exp(x)sin(x) + exp(x)cos(x)
    return std::exp(x) * std::sin(x);
}

int main() {
    std::cout << "Running AD Unit Tests..." << std::endl;

    // Test 1: Polynomial
    // x = 3.0, x' = 1.0
    Dual<double> x1 = { 3.0, 1.0 };
    Dual<double> y1 = simple_poly(x1);
    
    // Expected: f(3) = 9 + 6 + 1 = 16
    ASSERT_NEAR(y1.val, 16.0, 1e-9);
    
    // Expected: f'(3) = 2x + 2 = 2(3) + 2 = 8
    ASSERT_NEAR(y1.grad, 8.0, 1e-9);
    
    std::cout << "PASS: Polynomial" << std::endl;

    // Test 2: Transcendental
    // x = 0.5
    double val = 0.5;
    Dual<double> x2 = { val, 1.0 };
    Dual<double> y2 = transcendental(x2);
    
    double expect_val = std::exp(val) * std::sin(val);
    double expect_grad = std::exp(val) * std::sin(val) + std::exp(val) * std::cos(val);
    
    ASSERT_NEAR(y2.val, expect_val, 1e-9);
    ASSERT_NEAR(y2.grad, expect_grad, 1e-9);
    
    std::cout << "PASS: Transcendental" << std::endl;
    
    // Test 3: Min/Max Clamping
    // f(x) = min(x, 10.0)
    Dual<double> x3_low = { 5.0, 1.0 };
    Dual<double> y3_low = std::min(x3_low, 10.0);
    ASSERT_NEAR(y3_low.val, 5.0, 1e-9);
    ASSERT_NEAR(y3_low.grad, 1.0, 1e-9); // Gradient passes through

    Dual<double> x3_high = { 15.0, 1.0 };
    Dual<double> y3_high = std::min(x3_high, 10.0);
    ASSERT_NEAR(y3_high.val, 10.0, 1e-9);
    ASSERT_NEAR(y3_high.grad, 0.0, 1e-9); // Gradient blocked by clamp
    
    std::cout << "PASS: Min/Max" << std::endl;

    return 0;
}
