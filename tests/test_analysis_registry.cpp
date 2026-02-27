// ============================================================================
// tests/test_analysis_registry.cpp — Phase 4 AnalysisRegistry + StagedDispatcher
// ============================================================================
// Tests:
//   1. AnalysisRegistry::supports() returns true for AC (existing handler)
//   2. TRANSFER_FUNCTION, POLE_ZERO, FOURIER are registered via static init
//   3. StagedDispatcher selects SCALAR for a 5-node linear circuit
//   4. StagedDispatcher selects MULTICORE_CPU for a nonlinear heavy circuit
// ============================================================================

#include "compute/analysis_registry.h"
#include "compute/staged_dispatcher.h"
#include "compute/hardware_capabilities.h"
#include "compute/solvers/register_analyses.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

// Pull in static registrations from solver .cpp files.
// These translation units register themselves at static-init time.
// They must be linked (not just declared) — ensured by CMakeLists.

using namespace acutesim::compute;
using namespace acutesim::compute::orchestration;

static int gPassed = 0;
static int gFailed = 0;

#define EXPECT_TRUE(expr, msg) do { \
    if (!(expr)) { \
        std::printf("FAIL: %s\n", msg); \
        ++gFailed; \
    } else { \
        std::printf("PASS: %s\n", msg); \
        ++gPassed; \
    } \
} while (0)

#define EXPECT_EQ(a, b, msg) do { \
    if ((a) != (b)) { \
        std::printf("FAIL: %s  (got %d, expected %d)\n", msg, (int)(a), (int)(b)); \
        ++gFailed; \
    } else { \
        std::printf("PASS: %s\n", msg); \
        ++gPassed; \
    } \
} while (0)

// ── Test 1: Existing analyses are registered ─────────────────────────────────

static void testExistingAnalysesRegistered() {
    // TRANSFER_FUNCTION, POLE_ZERO, FOURIER are registered via static stubs
    // in tf_analysis.cpp, pz_analysis.cpp, fourier_analysis.cpp.
    auto& reg = AnalysisRegistry::instance();
    EXPECT_TRUE(reg.supports(AnalysisTypeDTO::TRANSFER_FUNCTION),
                "AnalysisRegistry::supports(TRANSFER_FUNCTION)");
    EXPECT_TRUE(reg.supports(AnalysisTypeDTO::POLE_ZERO),
                "AnalysisRegistry::supports(POLE_ZERO)");
    EXPECT_TRUE(reg.supports(AnalysisTypeDTO::FOURIER),
                "AnalysisRegistry::supports(FOURIER)");
}

// ── Test 2: Dispatch returns error for unknown type ──────────────────────────

static void testDispatchUnknownType() {
    auto& reg = AnalysisRegistry::instance();

    // AnalysisTypeDTO value 99 is not registered
    auto unknownType = static_cast<AnalysisTypeDTO>(99);
    EXPECT_TRUE(!reg.supports(unknownType),
                "AnalysisRegistry::supports(99) == false");
}

// ── Test 3: StagedDispatcher — small linear circuit → SCALAR ────────────────

static void testStagedDispatcherSmallLinear() {
    HardwareCapabilities hw;
    hw.simdTier = HardwareCapabilities::SIMDTier::SCALAR;
    hw.hasGPU   = false;

    StagedDispatcher disp(hw);
    CircuitMetrics m;
    m.nodeCount         = 5;
    m.activeDeviceCount = 0;
    m.isLinear          = true;

    auto tier = disp.selectTier(m);
    EXPECT_EQ(static_cast<int>(tier),
              static_cast<int>(ExecutionTier::SCALAR),
              "StagedDispatcher: 5-node linear → SCALAR");
}

// ── Test 4: StagedDispatcher — many nonlinear devices → MULTICORE_CPU ────────

static void testStagedDispatcherNonlinearHeavy() {
    HardwareCapabilities hw;
    hw.simdTier = HardwareCapabilities::SIMDTier::AVX2;
    hw.hasGPU   = false;

    StagedDispatcher disp(hw);
    CircuitMetrics m;
    m.nodeCount         = 100;
    m.activeDeviceCount = 60;   // > 50 → MULTICORE_CPU
    m.isLinear          = false;

    auto tier = disp.selectTier(m);
    EXPECT_EQ(static_cast<int>(tier),
              static_cast<int>(ExecutionTier::MULTICORE_CPU),
              "StagedDispatcher: 60 active devices → MULTICORE_CPU");
}

// ── Test 5: StagedDispatcher — large linear with GPU → GPU ──────────────────

static void testStagedDispatcherLargeLinearGPU() {
    HardwareCapabilities hw;
    hw.simdTier = HardwareCapabilities::SIMDTier::AVX2;
    hw.hasGPU   = true;

    StagedDispatcher disp(hw);
    CircuitMetrics m;
    m.nodeCount         = 600;
    m.activeDeviceCount = 0;
    m.isLinear          = true;

    auto tier = disp.selectTier(m);
    EXPECT_EQ(static_cast<int>(tier),
              static_cast<int>(ExecutionTier::GPU),
              "StagedDispatcher: 600-node linear + GPU → GPU");
}

// ── Test 6: registeredTypes() returns at least TF, PZ, FOURIER ──────────────

static void testRegisteredTypesContainPhase4() {
    auto& reg = AnalysisRegistry::instance();
    auto types = reg.registeredTypes();

    bool hasTF  = false, hasPZ = false, hasFourier = false;
    for (auto t : types) {
        if (t == AnalysisTypeDTO::TRANSFER_FUNCTION) hasTF = true;
        if (t == AnalysisTypeDTO::POLE_ZERO)         hasPZ = true;
        if (t == AnalysisTypeDTO::FOURIER)           hasFourier = true;
    }
    EXPECT_TRUE(hasTF,      "registeredTypes() contains TRANSFER_FUNCTION");
    EXPECT_TRUE(hasPZ,      "registeredTypes() contains POLE_ZERO");
    EXPECT_TRUE(hasFourier, "registeredTypes() contains FOURIER");
}

int main() {
    std::printf("=== test_analysis_registry ===\n");

    // Ensure Phase 4 handlers are registered (guards against linker dead-stripping)
    acutesim::compute::solvers::registerAllAnalyses();

    testExistingAnalysesRegistered();
    testDispatchUnknownType();
    testStagedDispatcherSmallLinear();
    testStagedDispatcherNonlinearHeavy();
    testStagedDispatcherLargeLinearGPU();
    testRegisteredTypesContainPhase4();

    std::printf("\n%d passed, %d failed\n", gPassed, gFailed);
    return (gFailed > 0) ? 1 : 0;
}
