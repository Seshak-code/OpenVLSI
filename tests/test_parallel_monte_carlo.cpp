// ============================================================================
// tests/test_parallel_monte_carlo.cpp — Parallel MC Determinism (Phase 4)
// ============================================================================
// Verifies that executeMonteCarlo() with JobScheduler(N) produces bit-for-bit
// identical results to sequential execution (no scheduler).
//
// Invariant: each sample uses seed ^ sample_index → independent, reproducible
// regardless of which thread executes which sample.
//
// Tests:
//   1. 100-sample sequential vs parallel — raw bit equality on every result
//   2. Statistics (mean/stddev) match within floating-point rounding
//   3. No failures in either run
//   4. runIndex ordering is stable (0..99)
//   5. Cancel mid-run drains without crash or hang
// ============================================================================

#include "compute/orchestration/driver/batch_execution_engine.h"
#include "compute/orchestration/driver/job_scheduler.h"
#include "engine_api/simulation_dto.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <vector>
#include <algorithm>
#include <cmath>

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
        std::printf("FAIL: %s  (got %lld, expected %lld)\n", msg, \
                    (long long)(a), (long long)(b)); \
        ++gFailed; \
    } else { \
        std::printf("PASS: %s\n", msg); \
        ++gPassed; \
    } \
} while (0)

#define EXPECT_NEAR(a, b, eps, msg) do { \
    double _diff = (double)(a) - (double)(b); \
    if (_diff < 0.0) _diff = -_diff; \
    if (_diff > (double)(eps)) { \
        std::printf("FAIL: %s  (diff=%.6g > eps=%.6g)\n", msg, _diff, (double)(eps)); \
        ++gFailed; \
    } else { \
        std::printf("PASS: %s\n", msg); \
        ++gPassed; \
    } \
} while (0)

// ── Mock solver ───────────────────────────────────────────────────────────────
// Pure function: output is fully determined by the override value.
// V = 5V supply / resistance  (Ohm's law, no circuit physics needed).
// This isolates the test to RNG seeding determinism, not solver correctness.

static SimulationResponseDTO mockSolver(
        AnalysisTypeDTO /*analysis*/,
        const std::vector<ParameterVariationDTO>& overrides,
        const CornerParametersDTO& /*corner*/)
{
    SimulationResponseDTO r;
    r.success = true;
    double rVal = overrides.empty() ? 1000.0 : overrides[0].overrideValue;
    // Guard against non-physical samples (negative resistance from RNG tail)
    if (rVal <= 0.0) rVal = 1.0;
    double vOut = 5.0 / rVal * 1000.0;   // V = I * R_load (I = 5/R, R_load = 1k)
    r.dcOperatingPoint = { vOut };
    r.probeValues["Vout"] = { vOut };
    return r;
}

// ── Helpers ───────────────────────────────────────────────────────────────────

static MonteCarloConfigDTO makeConfig(uint32_t samples, uint64_t seed)
{
    MonteCarloConfigDTO cfg;
    cfg.sampleCount = samples;
    cfg.seed = seed;
    MonteCarloVariationDTO var;
    var.componentName    = "R1";
    var.propertyName     = "resistance";
    var.distributionType = "Gaussian";
    var.nominalValue     = 1000.0;
    var.tolerance        = 0.05;   // 5% sigma
    cfg.variations.push_back(var);
    return cfg;
}

static SimulationRequestDTO makeBaseRequest()
{
    SimulationRequestDTO req;
    req.analysis = AnalysisTypeDTO::DC;
    req.corner   = CornerParametersDTO{};
    return req;
}

// Sort runResults by runIndex so sequential vs parallel ordering is comparable.
static void sortByRunIndex(SimulationBatchResponseDTO& resp)
{
    std::sort(resp.runResults.begin(), resp.runResults.end(),
              [](const SimulationResponseDTO& a, const SimulationResponseDTO& b){
                  return a.runIndex < b.runIndex;
              });
}

// ── Test 1: Bit-exact parity — 100 sequential vs 100 parallel ────────────────

static void testBitExactParity()
{
    const uint32_t N = 100;
    auto cfg     = makeConfig(N, 0xDEADBEEFull);
    auto baseReq = makeBaseRequest();

    BatchExecutionEngine engine;
    std::atomic<bool> cancelled{false};

    // Sequential (no scheduler)
    auto seqResp = engine.executeMonteCarlo(baseReq, cfg, mockSolver,
                                             nullptr, cancelled, nullptr);
    sortByRunIndex(seqResp);

    // Parallel (4 workers)
    JobScheduler sched(4);
    auto parResp = engine.executeMonteCarlo(baseReq, cfg, mockSolver,
                                             nullptr, cancelled, &sched);
    sched.shutdown();
    sortByRunIndex(parResp);

    EXPECT_EQ(seqResp.runResults.size(), static_cast<size_t>(N),
              "Sequential: exactly 100 results");
    EXPECT_EQ(parResp.runResults.size(), static_cast<size_t>(N),
              "Parallel: exactly 100 results");
    EXPECT_TRUE(!seqResp.hadFailures, "Sequential: no failures");
    EXPECT_TRUE(!parResp.hadFailures, "Parallel: no failures");

    // Per-sample raw bit equality
    bool allBitsMatch = true;
    for (size_t i = 0; i < seqResp.runResults.size() && i < parResp.runResults.size(); ++i) {
        const auto& s = seqResp.runResults[i];
        const auto& p = parResp.runResults[i];
        if (s.dcOperatingPoint.size() != p.dcOperatingPoint.size()) {
            allBitsMatch = false;
            break;
        }
        for (size_t j = 0; j < s.dcOperatingPoint.size(); ++j) {
            uint64_t sv = 0, pv = 0;
            std::memcpy(&sv, &s.dcOperatingPoint[j], sizeof(double));
            std::memcpy(&pv, &p.dcOperatingPoint[j], sizeof(double));
            if (sv != pv) { allBitsMatch = false; break; }
        }
        if (!allBitsMatch) break;
    }
    EXPECT_TRUE(allBitsMatch, "All 100 samples: bit-exact match seq vs parallel");
}

// ── Test 2: Statistics parity ─────────────────────────────────────────────────

static void testStatisticsParity()
{
    const uint32_t N = 100;
    auto cfg     = makeConfig(N, 0xCAFEBABEull);
    auto baseReq = makeBaseRequest();

    BatchExecutionEngine engine;
    std::atomic<bool> cancelled{false};

    auto seqResp = engine.executeMonteCarlo(baseReq, cfg, mockSolver,
                                             nullptr, cancelled, nullptr);

    JobScheduler sched(4);
    auto parResp = engine.executeMonteCarlo(baseReq, cfg, mockSolver,
                                             nullptr, cancelled, &sched);
    sched.shutdown();

    // Statistics are derived from identical per-sample values, so they must
    // match within floating-point accumulation tolerance (~1e-10 for N=100).
    const double kTol = 1e-10;

    bool hasMean = seqResp.statistics.mean.count("Vout") > 0 &&
                   parResp.statistics.mean.count("Vout") > 0;
    EXPECT_TRUE(hasMean, "Statistics: Vout mean exists in both responses");

    if (hasMean) {
        EXPECT_NEAR(seqResp.statistics.mean.at("Vout"),
                    parResp.statistics.mean.at("Vout"),
                    kTol, "Statistics: mean(Vout) matches seq vs parallel");
        EXPECT_NEAR(seqResp.statistics.stddev.at("Vout"),
                    parResp.statistics.stddev.at("Vout"),
                    kTol, "Statistics: stddev(Vout) matches seq vs parallel");
        EXPECT_NEAR(seqResp.statistics.minVal.at("Vout"),
                    parResp.statistics.minVal.at("Vout"),
                    kTol, "Statistics: min(Vout) matches seq vs parallel");
        EXPECT_NEAR(seqResp.statistics.maxVal.at("Vout"),
                    parResp.statistics.maxVal.at("Vout"),
                    kTol, "Statistics: max(Vout) matches seq vs parallel");
    }
}

// ── Test 3: runIndex ordering is 0..N-1, no gaps ─────────────────────────────

static void testRunIndexOrdering()
{
    const uint32_t N = 20;
    auto cfg     = makeConfig(N, 12345ull);
    auto baseReq = makeBaseRequest();

    BatchExecutionEngine engine;
    std::atomic<bool> cancelled{false};

    JobScheduler sched(4);
    auto resp = engine.executeMonteCarlo(baseReq, cfg, mockSolver,
                                          nullptr, cancelled, &sched);
    sched.shutdown();
    sortByRunIndex(resp);

    bool ordered = true;
    for (uint32_t i = 0; i < static_cast<uint32_t>(resp.runResults.size()); ++i) {
        if (resp.runResults[i].runIndex != i) { ordered = false; break; }
    }
    EXPECT_TRUE(ordered, "Parallel MC: runIndex is 0..N-1 with no gaps");
}

// ── Test 4: Different seeds produce different results ────────────────────────

static void testSeedIsolation()
{
    auto cfg1 = makeConfig(10, 0x1111111111111111ull);
    auto cfg2 = makeConfig(10, 0x2222222222222222ull);
    auto baseReq = makeBaseRequest();

    BatchExecutionEngine engine;
    std::atomic<bool> cancelled{false};

    auto resp1 = engine.executeMonteCarlo(baseReq, cfg1, mockSolver,
                                           nullptr, cancelled, nullptr);
    auto resp2 = engine.executeMonteCarlo(baseReq, cfg2, mockSolver,
                                           nullptr, cancelled, nullptr);
    sortByRunIndex(resp1);
    sortByRunIndex(resp2);

    // Different seeds must produce at least one differing result
    bool anyDiff = false;
    size_t n = std::min(resp1.runResults.size(), resp2.runResults.size());
    for (size_t i = 0; i < n; ++i) {
        const auto& a = resp1.runResults[i].dcOperatingPoint;
        const auto& b = resp2.runResults[i].dcOperatingPoint;
        if (!a.empty() && !b.empty() && a[0] != b[0]) { anyDiff = true; break; }
    }
    EXPECT_TRUE(anyDiff, "Different seeds produce different sample distributions");
}

// ── Test 5: Cancel mid-run — no crash, no hang ───────────────────────────────

static void testCancelMidRun()
{
    const uint32_t N = 50;
    auto cfg     = makeConfig(N, 99999ull);
    auto baseReq = makeBaseRequest();

    BatchExecutionEngine engine;
    std::atomic<bool> cancelled{false};

    // Solver that takes a bit of time and checks cancel flag
    auto slowSolver = [&cancelled](AnalysisTypeDTO,
                                    const std::vector<ParameterVariationDTO>& ov,
                                    const CornerParametersDTO&) -> SimulationResponseDTO {
        SimulationResponseDTO r;
        r.success = !cancelled.load();
        double rVal = ov.empty() ? 1000.0 : ov[0].overrideValue;
        if (rVal <= 0.0) rVal = 1.0;
        r.dcOperatingPoint = { 5.0 / rVal * 1000.0 };
        return r;
    };

    // Cancel after a short time
    JobScheduler cancelSched(2);
    std::atomic<bool> timerDone{false};
    cancelSched.enqueue([&]{
        // Set cancel after first few tasks are likely in-flight
        cancelled.store(true);
        timerDone.store(true);
    });

    // Should return without deadlock even when cancelled partway through
    auto resp = engine.executeMonteCarlo(baseReq, cfg, slowSolver,
                                          nullptr, cancelled, nullptr);
    cancelSched.shutdown();

    // Not asserting exact count since cancel can happen at any point;
    // just verify no crash and result object is valid.
    EXPECT_TRUE(resp.runResults.size() <= static_cast<size_t>(N),
                "Cancel mid-run: result count <= total (no overflow)");
    EXPECT_TRUE(timerDone.load(), "Cancel: timer task completed");
}

// ─────────────────────────────────────────────────────────────────────────────

int main()
{
    std::printf("=== test_parallel_monte_carlo ===\n");

    testBitExactParity();
    testStatisticsParity();
    testRunIndexOrdering();
    testSeedIsolation();
    testCancelMidRun();

    std::printf("\n%d passed, %d failed\n", gPassed, gFailed);
    return (gFailed > 0) ? 1 : 0;
}
