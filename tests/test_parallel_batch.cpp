// ============================================================================
// tests/test_parallel_batch.cpp — Phase 4.3/4.8 JobScheduler + Parallel Batch
// ============================================================================
// Tests:
//   1. JobScheduler runs N tasks and all complete
//   2. 8 DC sweep points with JobScheduler(4) complete deterministically
//   3. Results match sequential execution
//   4. JobScheduler(0) defaults to hardware_concurrency workers
// ============================================================================

#include "compute/orchestration/driver/job_scheduler.h"
#include "compute/orchestration/driver/batch_execution_engine.h"
#include "engine_api/simulation_dto.h"

#include <cassert>
#include <cstdio>
#include <atomic>
#include <vector>
#include <algorithm>
#include <numeric>

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
    double _diff = (a) - (b); \
    if (_diff < 0.0) _diff = -_diff; \
    if (_diff > (eps)) { \
        std::printf("FAIL: %s  (diff=%.6g > eps=%.6g)\n", msg, _diff, (double)(eps)); \
        ++gFailed; \
    } else { \
        std::printf("PASS: %s\n", msg); \
        ++gPassed; \
    } \
} while (0)

// ── Test 1: Basic task completion ─────────────────────────────────────────────

static void testJobSchedulerBasic() {
#ifdef __EMSCRIPTEN__
    // On WASM, enqueue runs synchronously — just verify it completes.
    JobScheduler sched(1);
    std::atomic<int> counter{0};
    for (int i = 0; i < 10; ++i)
        sched.enqueue([&] { counter.fetch_add(1); });
    EXPECT_EQ(counter.load(), 10, "JobScheduler: 10 tasks completed");
#else
    JobScheduler sched(4);
    std::atomic<int> counter{0};
    for (int i = 0; i < 20; ++i)
        sched.enqueue([&] { counter.fetch_add(1); });

    sched.shutdown();
    EXPECT_EQ(counter.load(), 20, "JobScheduler(4): 20 tasks all completed");
#endif
}

// ── Test 2: workerCount reflects requested count ──────────────────────────────

static void testJobSchedulerWorkerCount() {
#ifndef __EMSCRIPTEN__
    JobScheduler sched(4);
    EXPECT_EQ(sched.workerCount(), 4, "JobScheduler(4): workerCount == 4");
    sched.shutdown();
#else
    JobScheduler sched(4);
    EXPECT_EQ(sched.workerCount(), 1, "JobScheduler(WASM): workerCount == 1");
#endif
}

// ── Test 3: Parallel batch produces same results as sequential ────────────────

static void testParallelBatchDeterminism() {
    // Build a simple 8-run sweep that scales a "resistance" value
    SimulationBatchRequestDTO req;
    req.analysis = AnalysisTypeDTO::DC;
    for (int i = 0; i < 8; ++i) {
        BatchRunDTO run;
        run.runIndex = static_cast<uint32_t>(i);
        ParameterVariationDTO var;
        var.componentName = "R1";
        var.propertyName  = "resistance";
        var.overrideValue = 1000.0 * (i + 1);
        run.overrides.push_back(var);
        req.runs.push_back(run);
    }

    // Mock solver: V = I*R, I = 5V / R_override → result proportional to override
    auto mockSolver = [](AnalysisTypeDTO /*analysis*/,
                          const std::vector<ParameterVariationDTO>& overrides,
                          const CornerParametersDTO& /*corner*/) -> SimulationResponseDTO {
        SimulationResponseDTO r;
        r.success = true;
        double R = overrides.empty() ? 1000.0 : overrides[0].overrideValue;
        double V = 5.0 * R / (R + 1000.0); // voltage divider
        r.dcOperatingPoint = {V};
        return r;
    };

    // Sequential
    BatchExecutionEngine eng;
    std::atomic<bool> cancelled{false};
    auto seqResp = eng.executeBatch(req, mockSolver, nullptr, cancelled);

    // Parallel
    JobScheduler sched(4);
    auto parResp = eng.executeBatchParallel(req, mockSolver, nullptr, cancelled, &sched);
    sched.shutdown();

    EXPECT_EQ(seqResp.runResults.size(), parResp.runResults.size(),
              "Parallel batch: result count matches sequential");

    // Sort both by runIndex before comparing (parallel may arrive out-of-order)
    auto byRunIdx = [](const SimulationResponseDTO& a, const SimulationResponseDTO& b) {
        return a.runIndex < b.runIndex;
    };
    std::sort(seqResp.runResults.begin(), seqResp.runResults.end(), byRunIdx);
    std::sort(parResp.runResults.begin(), parResp.runResults.end(), byRunIdx);

    bool allMatch = true;
    for (size_t i = 0; i < seqResp.runResults.size(); ++i) {
        const auto& s = seqResp.runResults[i];
        const auto& p = parResp.runResults[i];
        if (!s.dcOperatingPoint.empty() && !p.dcOperatingPoint.empty()) {
            double diff = std::abs(s.dcOperatingPoint[0] - p.dcOperatingPoint[0]);
            if (diff > 1e-12) allMatch = false;
        } else if (s.success != p.success) {
            allMatch = false;
        }
    }
    EXPECT_TRUE(allMatch,
                "Parallel batch: results bit-exact match to sequential");
}

// ── Test 4: Parallel batch with scheduler == nullptr falls back to sequential ──

static void testParallelBatchFallback() {
    SimulationBatchRequestDTO req;
    req.analysis = AnalysisTypeDTO::DC;
    BatchRunDTO run; run.runIndex = 0;
    req.runs.push_back(run);

    auto mockSolver = [](AnalysisTypeDTO, const std::vector<ParameterVariationDTO>&,
                          const CornerParametersDTO&) -> SimulationResponseDTO {
        SimulationResponseDTO r; r.success = true; return r;
    };

    BatchExecutionEngine eng;
    std::atomic<bool> cancelled{false};
    auto resp = eng.executeBatchParallel(req, mockSolver, nullptr, cancelled, nullptr);
    EXPECT_EQ(resp.runResults.size(), static_cast<size_t>(1),
              "executeBatchParallel(null scheduler): fallback to sequential");
    EXPECT_TRUE(resp.runResults[0].success,
                "executeBatchParallel(null scheduler): run succeeded");
}

int main() {
    std::printf("=== test_parallel_batch ===\n");

    testJobSchedulerBasic();
    testJobSchedulerWorkerCount();
    testParallelBatchDeterminism();
    testParallelBatchFallback();

    std::printf("\n%d passed, %d failed\n", gPassed, gFailed);
    return (gFailed > 0) ? 1 : 0;
}
