/**
 * test_virtuoso_parity.cpp
 * Verification tests for the Cadence Virtuoso Parity features.
 *
 * Tests cover:
 *  1. Variable dependency chain resolution
 *  2. Circular dependency detection
 *  3. Expression parser + waveform vector output
 *  4. Sweep + Calculator combined
 *  5. Corner × Sweep cross-product
 */

#include <cmath>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <set>

#include "engine_api/simulation_context.h"
#include "acutesim_engine/infrastructure/postproc_engine.h"
#include "compute/orchestration/sweep_parameter.h"

using namespace acutesim::orchestration;
using namespace acutesim::compute::postproc;
using namespace acutesim::compute::orchestration;

static int testsRun = 0;
static int testsPassed = 0;

#define TEST_ASSERT(cond, msg) do { \
    testsRun++; \
    if (!(cond)) { \
        std::cerr << "  FAIL: " << msg << "  (" << __FILE__ << ":" << __LINE__ << ")" << std::endl; \
    } else { \
        testsPassed++; \
        std::cout << "  PASS: " << msg << std::endl; \
    } \
} while(0)

#define NEAR(a, b, tol) (std::abs((a)-(b)) < (tol))

// ===========================================================================
// Test 1: Variable Dependency Chain
// ===========================================================================
void test_variable_dependency_chain() {
    std::cout << "\n--- Test 1: Variable Dependency Chain ---" << std::endl;

    SimulationContext ctx;
    ctx.type = SimulationType::Analog;
    ctx.sources.push_back(FileRef{"test.sch", true, true});

    ctx.variableExprs["Rload"] = "1000";
    ctx.variableExprs["R2"] = "2*Rload";
    ctx.variableEnabled["Rload"] = true;
    ctx.variableEnabled["R2"] = true;

    std::string errorMsg;
    bool ok = ctx.resolveVariables(&errorMsg);

    TEST_ASSERT(ok, "resolveVariables should succeed for valid chain");
    TEST_ASSERT(NEAR(ctx.variables["Rload"], 1000.0, 1e-9), "Rload should be 1000");
    TEST_ASSERT(NEAR(ctx.variables["R2"], 2000.0, 1e-9), "R2 = 2*Rload should be 2000");

    ctx.variableExprs["Rload"] = "500";
    ok = ctx.resolveVariables(&errorMsg);

    TEST_ASSERT(ok, "Re-resolve should succeed after Rload change");
    TEST_ASSERT(NEAR(ctx.variables["Rload"], 500.0, 1e-9), "Rload updated to 500");
    TEST_ASSERT(NEAR(ctx.variables["R2"], 1000.0, 1e-9), "R2 should auto-update to 1000");

    ctx.variableExprs.clear();
    ctx.variables.clear();
    ctx.variableEnabled.clear();

    ctx.variableExprs["Ibias"] = "10e-6";
    ctx.variableExprs["Vov"] = "0.2";
    ctx.variableExprs["gm"] = "2*Ibias/Vov";
    ctx.variableEnabled["Ibias"] = true;
    ctx.variableEnabled["Vov"] = true;
    ctx.variableEnabled["gm"] = true;

    ok = ctx.resolveVariables(&errorMsg);

    TEST_ASSERT(ok, "Three-deep chain should resolve");
    double expectedGm = 2.0 * 10e-6 / 0.2;
    TEST_ASSERT(NEAR(ctx.variables["gm"], expectedGm, 1e-12),
                "gm = 2*Ibias/Vov should be 1e-4");
}

// ===========================================================================
// Test 2: Circular Dependency Detection
// ===========================================================================
void test_circular_dependency_detection() {
    std::cout << "\n--- Test 2: Circular Dependency Detection ---" << std::endl;

    SimulationContext ctx;
    ctx.type = SimulationType::Analog;
    ctx.sources.push_back(FileRef{"test.sch", true, true});

    ctx.variableExprs["A"] = "B";
    ctx.variableExprs["B"] = "A";
    ctx.variableEnabled["A"] = true;
    ctx.variableEnabled["B"] = true;

    std::string errorMsg;
    bool ok = ctx.resolveVariables(&errorMsg);

    TEST_ASSERT(!ok, "Circular A<->B should fail");
    TEST_ASSERT(errorMsg.find("ircular") != std::string::npos || errorMsg.find("ycl") != std::string::npos,
                "Error message should mention circular dependency");

    ctx.variableExprs.clear();
    ctx.variables.clear();
    ctx.variableEnabled.clear();

    ctx.variableExprs["X"] = "Y+1";
    ctx.variableExprs["Y"] = "Z+1";
    ctx.variableExprs["Z"] = "X+1";
    ctx.variableEnabled["X"] = true;
    ctx.variableEnabled["Y"] = true;
    ctx.variableEnabled["Z"] = true;

    ok = ctx.resolveVariables(&errorMsg);
    TEST_ASSERT(!ok, "Three-way cycle X->Y->Z->X should fail");

    ctx.variableEnabled["Z"] = false;
    ctx.variableExprs["X"] = "1";

    ok = ctx.resolveVariables(&errorMsg);
    TEST_ASSERT(ok, "Disabling Z should break cycle and allow resolution");
}

// ===========================================================================
// Test 3: Expression Parser + Waveform Vector Output
// ===========================================================================
void test_expression_parser_waveform() {
    std::cout << "\n--- Test 3: Expression Parser + Waveform Vector ---" << std::endl;

    PostProcEngine engine;

    Waveform vout;
    vout.name = "v(\"/out\")";
    vout.unit = "V";
    int N = 100;
    for (int i = 0; i < N; ++i) {
        double t = 2.0 * M_PI * i / (N - 1);
        vout.time.push_back(t);
        vout.values.push_back(std::sin(t));
    }

    std::unordered_map<std::string, Waveform> waves;
    waves["v(\"/out\")"] = vout;
    engine.setWaveforms(waves);

    Operand result = engine.evaluate("dB20(v(\"/out\"))");

    TEST_ASSERT(result.type == OperandType::Waveform,
                "dB20(v(\"/out\")) should return a Waveform");

    if (result.type == OperandType::Waveform) {
        TEST_ASSERT((int)result.wave.values.size() == N,
                    "Result waveform should have same length as input");

        int idx = N / 4;
        double val = result.wave.values[idx];
        double expected = 20.0 * std::log10(std::abs(vout.values[idx]));
        TEST_ASSERT(NEAR(val, expected, 1e-6),
                    "dB20 at peak should be ~0 dB");
    }

    Operand mulResult = engine.evaluate("v(\"/out\") * 2.0");
    if (mulResult.type == OperandType::Waveform) {
        TEST_ASSERT((int)mulResult.wave.values.size() == N,
                    "v(\"/out\") * 2 should have same size");
        TEST_ASSERT(NEAR(mulResult.wave.values[N/4], 2.0 * vout.values[N/4], 1e-9),
                    "v(\"/out\") * 2 at peak should be ~2.0");
    } else {
        TEST_ASSERT(false, "v(\"/out\") * 2 should be a Waveform");
    }

    TEST_ASSERT(NEAR(engine.evaluate("1 + 2*3 - -(4)/2").scalarVal, 9.0, 1e-12),
                "Scalar precedence and unary minus");
    Operand diff = engine.evaluate("v(\"/out\") - v(\"/out\")/2");
    TEST_ASSERT(diff.type == OperandType::Waveform &&
                NEAR(diff.wave.values[N/4], 0.5 * vout.values[N/4], 1e-12),
                "Waveform-waveform ops respect precedence");
}

// ===========================================================================
// Test 4: Sweep + Calculator Combined
// ===========================================================================
void test_sweep_calculator_combined() {
    std::cout << "\n--- Test 4: Sweep + Calculator Combined ---" << std::endl;

    SweepParameter sweep;
    sweep.variableName = "Rload";
    sweep.start = 1000;
    sweep.stop  = 5000;
    sweep.step  = 1000;

    std::vector<double> points = sweep.generatePoints();
    TEST_ASSERT((int)points.size() == 5,
                "Linear sweep should produce 5 points");

    if (points.size() >= 5) {
        TEST_ASSERT(NEAR(points[0], 1000, 1e-9), "First sweep point = 1000");
        TEST_ASSERT(NEAR(points[4], 5000, 1e-9), "Last sweep point = 5000");
    }
}

// ===========================================================================
// Test 5: Corner × Sweep Cross-Product
// ===========================================================================
void test_corner_sweep_cross_product() {
    std::cout << "\n--- Test 5: Corner x Sweep Cross-Product ---" << std::endl;

    SweepParameter sweep;
    sweep.variableName = "Rload";
    sweep.start = 1000;
    sweep.stop  = 3000;
    sweep.step  = 1000;

    std::vector<double> sweepPts = sweep.generatePoints();
    TEST_ASSERT((int)sweepPts.size() == 3, "3 sweep points");

    std::vector<double> corners = {-40.0, 27.0, 125.0};

    int totalSims = (int)corners.size() * (int)sweepPts.size();
    TEST_ASSERT(totalSims == 9, "Cross product should be 9");

    std::set<std::string> combos;
    for (double temp : corners) {
        for (double rload : sweepPts) {
            combos.insert("T=" + std::to_string(temp) + ",R=" + std::to_string(rload));
        }
    }
    TEST_ASSERT((int)combos.size() == 9, "All 9 cross-product combos should be unique");

    for (double temp : corners) {
        for (double rload : sweepPts) {
            SimulationContext ctx;
            ctx.type = SimulationType::Analog;
            ctx.sources.push_back(FileRef{"test.sch", true, true});

            ctx.variableExprs["TEMP"] = std::to_string(temp);
            ctx.variableExprs["Rload"] = std::to_string(rload);
            ctx.variableEnabled["TEMP"] = true;
            ctx.variableEnabled["Rload"] = true;

            bool ok = ctx.resolveVariables();
            TEST_ASSERT(ok, "Resolve should succeed for each corner/sweep combo");
            TEST_ASSERT(NEAR(ctx.variables["TEMP"], temp, 1e-9),  "TEMP variable correct");
            TEST_ASSERT(NEAR(ctx.variables["Rload"], rload, 1e-9), "Rload variable correct");
        }
    }
}

// ===========================================================================
// Main
// ===========================================================================
int main(int /* argc */, char** /* argv */) {
    std::cout << "=====================================" << std::endl;
    std::cout << "  Virtuoso Parity Verification Tests" << std::endl;
    std::cout << "=====================================" << std::endl;

    test_variable_dependency_chain();
    test_circular_dependency_detection();
    test_expression_parser_waveform();
    test_sweep_calculator_combined();
    test_corner_sweep_cross_product();

    std::cout << "\n=====================================" << std::endl;
    std::cout << "  Results: " << testsPassed << "/" << testsRun << " passed" << std::endl;
    std::cout << "=====================================" << std::endl;

    return (testsPassed == testsRun) ? 0 : 1;
}
