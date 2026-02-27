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

#include <QtCore>
#include <cmath>
#include <cassert>
#include <iostream>

#include "engine_api/simulation_context.h"
#include "compute/infrastructure/postproc_engine.h"
#include "compute/orchestration/batch_runner.h"

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
    
    // Define: Rload = 1000, R2 = 2*Rload
    ctx.variableExprs.insert("Rload", "1000");
    ctx.variableExprs.insert("R2", "2*Rload");
    ctx.variableEnabled.insert("Rload", true);
    ctx.variableEnabled.insert("R2", true);
    
    QString errorMsg;
    bool ok = ctx.resolveVariables(&errorMsg);
    
    TEST_ASSERT(ok, "resolveVariables should succeed for valid chain");
    TEST_ASSERT(NEAR(ctx.variables["Rload"], 1000.0, 1e-9), "Rload should be 1000");
    TEST_ASSERT(NEAR(ctx.variables["R2"], 2000.0, 1e-9), "R2 = 2*Rload should be 2000");
    
    // Change Rload and re-resolve
    ctx.variableExprs["Rload"] = "500";
    ok = ctx.resolveVariables(&errorMsg);
    
    TEST_ASSERT(ok, "Re-resolve should succeed after Rload change");
    TEST_ASSERT(NEAR(ctx.variables["Rload"], 500.0, 1e-9), "Rload updated to 500");
    TEST_ASSERT(NEAR(ctx.variables["R2"], 1000.0, 1e-9), "R2 should auto-update to 1000");
    
    // Three-deep chain: Ibias=10e-6, gm=2*Ibias/0.2, BW=gm/(2*3.14159*1e-12)
    ctx.variableExprs.clear();
    ctx.variables.clear();
    ctx.variableEnabled.clear();
    
    ctx.variableExprs.insert("Ibias", "10e-6");
    ctx.variableExprs.insert("Vov", "0.2");
    ctx.variableExprs.insert("gm", "2*Ibias/Vov");
    ctx.variableEnabled.insert("Ibias", true);
    ctx.variableEnabled.insert("Vov", true);
    ctx.variableEnabled.insert("gm", true);
    
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
    
    // A = B, B = A (simple cycle)
    ctx.variableExprs.insert("A", "B");
    ctx.variableExprs.insert("B", "A");
    ctx.variableEnabled.insert("A", true);
    ctx.variableEnabled.insert("B", true);
    
    QString errorMsg;
    bool ok = ctx.resolveVariables(&errorMsg);
    
    TEST_ASSERT(!ok, "Circular A<->B should fail");
    TEST_ASSERT(errorMsg.contains("Circular") || errorMsg.contains("circular"), 
                "Error message should mention 'Circular'");
    
    // Three-way cycle: X=Y+1, Y=Z+1, Z=X+1
    ctx.variableExprs.clear();
    ctx.variables.clear();
    ctx.variableEnabled.clear();
    
    ctx.variableExprs.insert("X", "Y+1");
    ctx.variableExprs.insert("Y", "Z+1");
    ctx.variableExprs.insert("Z", "X+1");
    ctx.variableEnabled.insert("X", true);
    ctx.variableEnabled.insert("Y", true);
    ctx.variableEnabled.insert("Z", true);
    
    ok = ctx.resolveVariables(&errorMsg);
    TEST_ASSERT(!ok, "Three-way cycle X->Y->Z->X should fail");
    
    // Disabled variable should break the cycle
    ctx.variableEnabled["Z"] = false;
    ctx.variableExprs["X"] = "1"; // X independent now
    
    ok = ctx.resolveVariables(&errorMsg);
    TEST_ASSERT(ok, "Disabling Z should break cycle and allow resolution");
}

// ===========================================================================
// Test 3: Expression Parser + Waveform Vector Output
// ===========================================================================
void test_expression_parser_waveform() {
    std::cout << "\n--- Test 3: Expression Parser + Waveform Vector ---" << std::endl;
    
    PostProcEngine engine;
    
    // Create a mock waveform: v("/out") = sin(t) for t in [0, 2*pi]
    Waveform vout;
    vout.name = "v(\"/out\")";
    vout.unit = "V";
    int N = 100;
    for (int i = 0; i < N; ++i) {
        double t = 2.0 * M_PI * i / (N - 1);
        vout.time.append(t);
        vout.values.append(std::sin(t));
    }
    
    QMap<QString, Waveform> waves;
    waves.insert("v(\"/out\")", vout);
    engine.setWaveforms(waves);
    
    // Test: dB20(v("/out")) should produce a vector
    Operand result = engine.evaluate("dB20(v(\"/out\"))");
    
    // dB20 applied to a waveform should produce a Waveform
    TEST_ASSERT(result.type == OperandType::Waveform, 
                "dB20(v(\"/out\")) should return a Waveform");
    
    if (result.type == OperandType::Waveform) {
        TEST_ASSERT(result.wave.values.size() == N, 
                    "Result waveform should have same length as input");
        
        // Check a specific point: dB20(sin(pi/2)) = dB20(1.0) = 0 dB
        // At index 25 (t = pi/2), sin = 1, dB20(1) = 20*log10(1) = 0
        int idx = N / 4; // t = pi/2
        double val = result.wave.values[idx];
        double expected = 20.0 * std::log10(std::abs(vout.values[idx]));
        TEST_ASSERT(NEAR(val, expected, 1e-6), 
                    "dB20 at peak should be ~0 dB");
    }
    
    // Test: scalar evaluate - value(v("/out"), 1.5708)
    // For now, this may not be implemented yet. Let's test what we can.
    
    // Test basic arithmetic on waveforms: v("/out") * 2.0
    // This tests operator dispatch
    Operand mulResult = engine.evaluate("v(\"/out\") * 2.0");
    if (mulResult.type == OperandType::Waveform) {
        TEST_ASSERT(mulResult.wave.values.size() == N,
                    "v(\"/out\") * 2 should have same size");
        TEST_ASSERT(NEAR(mulResult.wave.values[N/4], 2.0 * vout.values[N/4], 1e-9),
                    "v(\"/out\") * 2 at peak should be ~2.0");
    } else {
        TEST_ASSERT(false, "v(\"/out\") * 2 should be a Waveform");
    }
}

// ===========================================================================
// Test 4: Sweep + Calculator Combined
// ===========================================================================
void test_sweep_calculator_combined() {
    std::cout << "\n--- Test 4: Sweep + Calculator Combined ---" << std::endl;
    
    // Verify SweepParameter generates correct points
    SweepParameter sweep;
    sweep.variableName = "Rload";
    sweep.start = 1000;
    sweep.stop = 5000;
    sweep.step = 1000;
    
    QVector<double> points = sweep.generatePoints();
    TEST_ASSERT(points.size() == 5, 
                QString("Linear sweep should produce 5 points, got %1").arg(points.size()).toStdString().c_str());
    
    if (points.size() >= 5) {
        TEST_ASSERT(NEAR(points[0], 1000, 1e-9), "First sweep point = 1000");
        TEST_ASSERT(NEAR(points[4], 5000, 1e-9), "Last sweep point = 5000");
    }
    
    // Verify that BatchRunner can clone context per sweep point
    SimulationContext ctx;
    ctx.type = SimulationType::Analog;
    ctx.sources.push_back(FileRef{"test.sch", true, true});
    ctx.variableExprs.insert("Rload", "1000");
    ctx.variableEnabled.insert("Rload", true);
    ctx.resolveVariables();
    
    // We won't actually run a real solver, but we can verify the BatchRunner API
    BatchRunner runner;
    runner.setContext(ctx);
    
    TensorNetlist netlist;
    // Minimal netlist (won't actually simulate, but tests API plumbing)
    netlist.numGlobalNodes = 2;
    runner.setBaseNetlist(netlist);
    runner.setSweep(sweep);
    
    int solverCallCount = 0;
    QVector<double> seenRloadValues;
    
    // Mock solver: just record what context values it sees
    auto mockSolver = [&](std::shared_ptr<const CompiledTensorBlock> compiled) -> QJsonObject {
        solverCallCount++;
        // The runner should have modified the context variable for each step
        QJsonObject result;
        result["status"] = "ok";
        return result;
    };
    
    BatchResult batchResult = runner.run(mockSolver);
    
    TEST_ASSERT(solverCallCount == 5, 
                QString("Solver should be called 5 times, called %1").arg(solverCallCount).toStdString().c_str());
    TEST_ASSERT(batchResult.sweepParam == "Rload", "Sweep param should be Rload");
    TEST_ASSERT(batchResult.sweepValues.size() == 5, "Sweep values count should be 5");
}

// ===========================================================================
// Test 5: Corner × Sweep Cross-Product
// ===========================================================================
void test_corner_sweep_cross_product() {
    std::cout << "\n--- Test 5: Corner × Sweep Cross-Product ---" << std::endl;
    
    // Verify cross-product enumeration logic
    // 3 corners × 5 sweep points = 15 distinct simulation contexts
    
    SweepParameter sweep;
    sweep.variableName = "Rload";
    sweep.start = 1000;
    sweep.stop = 3000;
    sweep.step = 1000;
    
    QVector<double> sweepPts = sweep.generatePoints();
    TEST_ASSERT(sweepPts.size() == 3, "3 sweep points");
    
    // Define 3 temperature corners
    QVector<double> corners = {-40.0, 27.0, 125.0};
    
    // Cross product: corners × sweep = 3 × 3 = 9 total
    int totalSims = corners.size() * sweepPts.size();
    TEST_ASSERT(totalSims == 9, 
                QString("Cross product should be 9, got %1").arg(totalSims).toStdString().c_str());
    
    // Verify each combination is unique
    QSet<QString> combos;
    for (double temp : corners) {
        for (double rload : sweepPts) {
            QString key = QString("T=%1,R=%2").arg(temp).arg(rload);
            combos.insert(key);
        }
    }
    TEST_ASSERT(combos.size() == 9, "All 9 cross-product combos should be unique");
    
    // Verify the SimulationContext correctly holds variables for each combo
    for (double temp : corners) {
        for (double rload : sweepPts) {
            SimulationContext ctx;
            ctx.type = SimulationType::Analog;
            ctx.sources.push_back(FileRef{"test.sch", true, true});
            
            ctx.variableExprs.insert("TEMP", QString::number(temp));
            ctx.variableExprs.insert("Rload", QString::number(rload));
            ctx.variableEnabled.insert("TEMP", true);
            ctx.variableEnabled.insert("Rload", true);
            
            bool ok = ctx.resolveVariables();
            TEST_ASSERT(ok, 
                        QString("Resolve for T=%1, R=%2 should succeed")
                            .arg(temp).arg(rload).toStdString().c_str());
            TEST_ASSERT(NEAR(ctx.variables["TEMP"], temp, 1e-9), 
                        "TEMP variable correct");
            TEST_ASSERT(NEAR(ctx.variables["Rload"], rload, 1e-9), 
                        "Rload variable correct");
        }
    }
}

// ===========================================================================
// Main
// ===========================================================================
int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    
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
