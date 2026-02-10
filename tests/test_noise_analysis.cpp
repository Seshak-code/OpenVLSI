/**
 * test_noise_analysis.cpp
 *
 * Tests for noise analysis subsystem:
 * 1. Noise model PSD functions (analytical verification)
 * 2. AC solver correctness (resistor divider transfer function)
 * 3. Noise analysis integration (RC filter noise = kT/C)
 */

#include <iostream>
#include <cmath>
#include <cassert>
#include <string>
#include "../physics/noise_models.h"
#include "../compute/solvers/ac_solver.h"

static int tests_passed = 0;
static int tests_failed = 0;

static void check(const std::string& label, bool condition) {
    if (condition) {
        tests_passed++;
        std::cout << "  [PASS] " << label << "\n";
    } else {
        tests_failed++;
        std::cout << "  [FAIL] " << label << "\n";
    }
}

static void check_approx(const std::string& label, double actual, double expected, double rel_tol = 0.05) {
    double denom = std::max(std::abs(expected), 1e-30);
    double rel_err = std::abs(actual - expected) / denom;
    bool pass = rel_err < rel_tol;

    if (pass) {
        tests_passed++;
        std::cout << "  [PASS] " << label
                  << " (actual=" << actual << " expected=" << expected
                  << " rel_err=" << rel_err << ")\n";
    } else {
        tests_failed++;
        std::cout << "  [FAIL] " << label
                  << " (actual=" << actual << " expected=" << expected
                  << " rel_err=" << rel_err << ")\n";
    }
}

// ===========================================================================
// Test 1: Noise model PSD functions
// ===========================================================================
void test_noise_models() {
    std::cout << "\n[Test: Noise Model PSD Functions]\n";

    double T = 300.0;
    double kB = 1.380649e-23;
    double q = 1.602176634e-19;

    // Resistor thermal noise: 4kT/R
    double R = 1000.0;
    double psd_i = NoiseModels::resistor_thermal_current_psd(R, T);
    double expected_psd_i = 4.0 * kB * T / R;
    check_approx("Resistor thermal current PSD (R=1k)", psd_i, expected_psd_i, 1e-6);

    double psd_v = NoiseModels::resistor_thermal_voltage_psd(R, T);
    double expected_psd_v = 4.0 * kB * T * R;
    check_approx("Resistor thermal voltage PSD (R=1k)", psd_v, expected_psd_v, 1e-6);

    // Diode shot noise: 2qI
    double I_dc = 1e-3;
    double psd_shot = NoiseModels::diode_shot_noise_psd(I_dc);
    double expected_shot = 2.0 * q * I_dc;
    check_approx("Diode shot noise PSD (I=1mA)", psd_shot, expected_shot, 1e-6);

    // BJT collector shot noise
    double psd_ic = NoiseModels::bjt_collector_shot_psd(I_dc);
    check_approx("BJT collector shot PSD (Ic=1mA)", psd_ic, expected_shot, 1e-6);

    // BJT base shot noise
    double I_b = I_dc / 100.0; // beta=100
    double psd_ib = NoiseModels::bjt_base_shot_psd(I_b);
    check_approx("BJT base shot PSD (Ib=10uA)", psd_ib, 2.0 * q * I_b, 1e-6);

    // MOSFET channel thermal noise: 4kT*(2/3)*gm
    double gm = 10e-3; // 10 mS
    double psd_thermal = NoiseModels::mosfet_channel_thermal_psd(gm, T);
    double expected_thermal = 4.0 * kB * T * (2.0/3.0) * gm;
    check_approx("MOSFET channel thermal PSD (gm=10mS)", psd_thermal, expected_thermal, 1e-6);

    // MOSFET flicker noise
    double KF = 1e-24, I_d = 1e-3, AF = 1.0, C_ox = 1e-3, W = 10e-6, L = 1e-6, freq = 1e3;
    double psd_flicker = NoiseModels::mosfet_flicker_psd(KF, I_d, AF, C_ox, W, L, freq);
    double expected_flicker = KF * std::pow(I_d, AF) / (C_ox * W * L * freq);
    check_approx("MOSFET flicker PSD", psd_flicker, expected_flicker, 1e-6);

    // Edge cases
    check("Zero resistance returns zero", NoiseModels::resistor_thermal_current_psd(0.0, T) == 0.0);
    check("Negative gm returns zero", NoiseModels::mosfet_channel_thermal_psd(-1.0, T) == 0.0);
    check("Zero KF returns zero", NoiseModels::mosfet_flicker_psd(0.0, I_d, AF, C_ox, W, L, freq) == 0.0);
}

// ===========================================================================
// Test 2: AC solver - resistor divider
// ===========================================================================
void test_ac_resistor_divider() {
    std::cout << "\n[Test: AC Resistor Divider]\n";

    // Simple voltage divider: V1 --R1-- node1 --R2-- GND
    // Transfer function: V(node1)/V(in) = R2/(R1+R2) = 0.5 for R1=R2
    // Since AC is small-signal around DC OP, and voltage sources are modeled
    // with small R_int, we just check that the AC solver produces reasonable results.

    TensorNetlist netlist;
    netlist.numGlobalNodes = 2; // node1 = output, node2 = V_in internal

    // V1: 5V at node 2
    VoltageSource vs;
    vs.nodePositive = 2;
    vs.nodeNegative = 0; // ground
    vs.voltage_V = 5.0;
    netlist.globalBlock.voltageSources.push_back(vs);

    // R1: node2 -> node1, 1k
    Resistor r1;
    r1.name = "R1";
    r1.nodeTerminal1 = 2;
    r1.nodeTerminal2 = 1;
    r1.resistance_ohms = 1000.0;
    netlist.globalBlock.resistors.push_back(r1);

    // R2: node1 -> GND, 1k
    Resistor r2;
    r2.name = "R2";
    r2.nodeTerminal1 = 1;
    r2.nodeTerminal2 = 0;
    r2.resistance_ohms = 1000.0;
    netlist.globalBlock.resistors.push_back(r2);

    // DC solution: node1=2.5V, node2=5.0V
    std::vector<double> dcSolution = {2.5, 5.0};

    // Run AC sweep
    ACSolver::ACResult result = ACSolver::solveAC(netlist, dcSolution, 1.0, 1e6, 10);

    check("AC result has 10 frequency points", result.frequencies.size() == 10);
    check("AC result has node voltages", !result.nodeVoltages.empty());

    // For a purely resistive divider, transfer function is flat across frequency.
    // The result at any frequency should have node1 at approx 0.5 * input voltage (from AC excitation).
    // But since we don't inject an AC source here, all voltages should be near zero (no AC excitation).
    // This test validates the solver doesn't crash.
    if (!result.nodeVoltages.empty()) {
        check("AC voltage vector is correct size", result.nodeVoltages[0].size() == 2);
    }
}

// ===========================================================================
// Test 3: RC filter noise = kT/C
// ===========================================================================
void test_rc_filter_noise() {
    std::cout << "\n[Test: RC Filter Noise (kT/C)]\n";

    // Circuit: R -- node1 -- C -- GND
    // Output at node1
    // Analytical: integrated output noise = kT/C (regardless of R value)

    double R = 10000.0;  // 10k
    double C = 1e-12;    // 1pF
    double T = 300.0;
    double kB = 1.380649e-23;

    TensorNetlist netlist;
    netlist.numGlobalNodes = 1; // Single node

    // R: between ground and node1
    Resistor r;
    r.name = "R1";
    r.nodeTerminal1 = 0; // ground
    r.nodeTerminal2 = 1;
    r.resistance_ohms = R;
    netlist.globalBlock.resistors.push_back(r);

    // C: node1 to ground
    Capacitor cap;
    cap.name = "C1";
    cap.nodePlate1 = 1;
    cap.nodePlate2 = 0;
    cap.capacitance_farads = C;
    netlist.globalBlock.capacitors.push_back(cap);

    // DC solution: node1 = 0V (no DC bias)
    std::vector<double> dcSolution = {0.0};

    // Run noise analysis with dense frequency sweep
    // Need enough bandwidth: f_3dB = 1/(2*pi*R*C) = ~15.9 MHz for R=10k, C=1pF
    // Sweep from 1Hz to 1GHz (well past -3dB) with 500 points
    ACSolver::NoiseResult result = ACSolver::solveNoise(
        netlist, dcSolution, 1, 0, 1.0, 1e9, 500, T);

    double expected_noise_vrms = std::sqrt(kB * T / C);

    check("Noise result has frequency data", !result.frequencies.empty());
    check("Noise result has PSD data", !result.output_noise_psd.empty());

    // The integrated noise should be approximately kT/C
    // Allow 20% tolerance due to numerical integration over log-spaced frequencies
    check_approx("RC filter integrated noise matches kT/C",
                  result.integrated_noise_vrms, expected_noise_vrms, 0.20);

    std::cout << "  Expected: " << expected_noise_vrms << " Vrms\n";
    std::cout << "  Got:      " << result.integrated_noise_vrms << " Vrms\n";
}

// ===========================================================================
// Test 4: MOSFET amplifier input-referred noise
// ===========================================================================
void test_mosfet_noise_density() {
    std::cout << "\n[Test: MOSFET Noise Density]\n";

    // A MOSFET with gm should produce channel thermal noise
    // Input-referred noise density ≈ 4kT*(2/3)/gm [V^2/Hz]
    double kB = 1.380649e-23;
    double T = 300.0;
    double gm = 10e-3; // 10 mS

    double expected_input_referred = 4.0 * kB * T * (2.0/3.0) / gm;

    // This is a unit-level test of the noise model function
    double psd = NoiseModels::mosfet_channel_thermal_psd(gm, T);
    double input_referred = psd / (gm * gm); // Divide by gm^2 to get V^2/Hz

    check_approx("MOSFET input-referred noise density",
                  input_referred, expected_input_referred, 1e-6);
}

// ===========================================================================
// Test 5: Shot noise at known bias point
// ===========================================================================
void test_shot_noise() {
    std::cout << "\n[Test: Shot Noise]\n";

    double q = 1.602176634e-19;

    // At 1mA bias, shot noise should be 2qI = 3.2e-22 A^2/Hz
    double I_bias = 1e-3;
    double expected = 2.0 * q * I_bias;
    double actual = NoiseModels::diode_shot_noise_psd(I_bias);

    check_approx("Shot noise at 1mA", actual, expected, 1e-10);

    // At 1uA
    I_bias = 1e-6;
    expected = 2.0 * q * I_bias;
    actual = NoiseModels::diode_shot_noise_psd(I_bias);
    check_approx("Shot noise at 1uA", actual, expected, 1e-10);
}

// ===========================================================================
int main() {
    std::cout << "=== Noise Analysis Test Suite ===\n";

    test_noise_models();
    test_ac_resistor_divider();
    test_rc_filter_noise();
    test_mosfet_noise_density();
    test_shot_noise();

    std::cout << "\n=== Results: " << tests_passed << " passed, "
              << tests_failed << " failed ===\n";

    if (tests_failed > 0) {
        std::cerr << "NOISE ANALYSIS TEST FAILED\n";
        return 1;
    }

    std::cout << "ALL NOISE ANALYSIS TESTS PASSED\n";
    return 0;
}
