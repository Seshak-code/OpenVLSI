// ============================================================================
// tests/test_analysis_registry.cpp — Phase 4 AnalysisRegistry
// ============================================================================
// Tests:
//   1. AnalysisRegistry::supports() returns true for AC (existing handler)
//   2. TRANSFER_FUNCTION, FOURIER are registered via static init
// ============================================================================

#include "acutesim_engine/solvers/analysis_registry.h"
#include "acutesim_engine/solvers/register_analyses.h"

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
    // TRANSFER_FUNCTION, FOURIER are registered via static stubs
    // in tf_analysis.cpp, fourier_analysis.cpp.
    auto& reg = AnalysisRegistry::instance();
    EXPECT_TRUE(reg.supports(AnalysisTypeDTO::TRANSFER_FUNCTION),
                "AnalysisRegistry::supports(TRANSFER_FUNCTION)");
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

// ── Test 6: registeredTypes() returns at least TF, FOURIER ──────────────

static void testRegisteredTypesContainPhase4() {
    auto& reg = AnalysisRegistry::instance();
    auto types = reg.registeredTypes();

    bool hasTF  = false, hasFourier = false;
    for (auto t : types) {
        if (t == AnalysisTypeDTO::TRANSFER_FUNCTION) hasTF = true;
        if (t == AnalysisTypeDTO::FOURIER)           hasFourier = true;
    }
    EXPECT_TRUE(hasTF,      "registeredTypes() contains TRANSFER_FUNCTION");
    EXPECT_TRUE(hasFourier, "registeredTypes() contains FOURIER");
}

int main() {
    std::printf("=== test_analysis_registry ===\n");

    // Ensure Phase 4 handlers are registered (guards against linker dead-stripping)
    acutesim::compute::solvers::registerAllAnalyses();

    testExistingAnalysesRegistered();
    testDispatchUnknownType();
    testRegisteredTypesContainPhase4();

    std::printf("\n%d passed, %d failed\n", gPassed, gFailed);
    return (gFailed > 0) ? 1 : 0;
}
