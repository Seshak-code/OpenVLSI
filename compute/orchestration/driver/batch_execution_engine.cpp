#include <cstdint>
#include "batch_execution_engine.h"
#include "job_scheduler.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <mutex>
#include <vector>
#include "acutesim_engine/math/portable_rng.h"

namespace acutesim {
namespace compute {
namespace orchestration {

SimulationBatchResponseDTO BatchExecutionEngine::executeBatch(
    const SimulationBatchRequestDTO& request,
    RunSolver solver,
    ProgressFn progress,
    const std::atomic<bool>& cancelled)
{
    SimulationBatchResponseDTO batchResp;
    batchResp.sessionToken = request.sessionToken;

    uint32_t total = static_cast<uint32_t>(request.runs.size());
    uint32_t completed = 0;

    // Sort runs by runIndex for deterministic ordering
    std::vector<const BatchRunDTO*> sortedRuns;
    sortedRuns.reserve(total);
    for (const auto& run : request.runs) {
        sortedRuns.push_back(&run);
    }
    std::sort(sortedRuns.begin(), sortedRuns.end(),
        [](const BatchRunDTO* a, const BatchRunDTO* b) {
            return a->runIndex < b->runIndex;
        });

    for (const BatchRunDTO* run : sortedRuns) {
        if (cancelled.load()) break;

        SimulationResponseDTO resp = solver(
            request.analysis,
            run->overrides,
            request.corner);

        resp.runIndex = static_cast<int32_t>(run->runIndex);

        if (!resp.success) batchResp.hadFailures = true;
        batchResp.runResults.push_back(std::move(resp));

        ++completed;
        if (progress) progress(completed, total);
    }

    return batchResp;
}

// ── Phase 4.8: Parallel Batch Execution ─────────────────────────────────────

SimulationBatchResponseDTO BatchExecutionEngine::executeBatchParallel(
    const SimulationBatchRequestDTO& request,
    RunSolver solver,
    ProgressFn progress,
    const std::atomic<bool>& cancelled,
    JobScheduler* scheduler)
{
    // Fallback to sequential if no scheduler provided.
    if (!scheduler) {
        return executeBatch(request, solver, progress, cancelled);
    }

    SimulationBatchResponseDTO batchResp;
    batchResp.sessionToken = request.sessionToken;

    uint32_t total = static_cast<uint32_t>(request.runs.size());
    if (total == 0) return batchResp;

    // Pre-allocate result slots so we can write by index without a lock.
    batchResp.runResults.resize(total);
    std::atomic<uint32_t> completed{0};
    std::mutex              failureMu;

    // Sort run indices for deterministic assignment
    std::vector<uint32_t> sortedIdx;
    sortedIdx.reserve(total);
    for (const auto& run : request.runs)
        sortedIdx.push_back(run.runIndex);
    std::sort(sortedIdx.begin(), sortedIdx.end());

    // Map runIndex → position in request.runs
    std::unordered_map<uint32_t, const BatchRunDTO*> runMap;
    for (const auto& run : request.runs)
        runMap[run.runIndex] = &run;

    // Barrier: every submitted task counts itself done under barrierMu. The waiter waits for all of them, cancelled
    // or not: the tasks hold references into this frame, and a cancelled task returns at once. (The count used to
    // be bumped and notified outside the mutex, so the last notify could fall between the waiter's check and its
    // sleep: a hang. Returning on `cancelled` left queued tasks writing into a dead frame.)
    std::mutex              barrierMu;
    std::condition_variable barrierCV;
    uint32_t                doneCount = 0, submitted = 0;

    for (uint32_t slot = 0; slot < total; ++slot) {
        if (cancelled.load()) break;
        ++submitted;

        uint32_t idx = sortedIdx[slot];
        const BatchRunDTO* run = runMap.at(idx);

        scheduler->enqueue([this, slot, run, &request, &solver,
                             &progress, &cancelled,
                             &batchResp, &completed, &failureMu,
                             &barrierMu, &barrierCV, &doneCount, total]() {
            if (cancelled.load()) {
                SimulationResponseDTO r;
                r.success = false;
                r.errorDetail = "Cancelled";
                r.runIndex = static_cast<int32_t>(run->runIndex);
                batchResp.runResults[slot] = std::move(r);
            } else {
                SimulationResponseDTO resp = solver(
                    request.analysis,
                    run->overrides,
                    request.corner);
                resp.runIndex = static_cast<int32_t>(run->runIndex);

                if (!resp.success) {
                    std::lock_guard<std::mutex> lk(failureMu);
                    batchResp.hadFailures = true;
                }
                batchResp.runResults[slot] = std::move(resp);
            }

            uint32_t done = completed.fetch_add(1) + 1;
            if (progress) progress(done, total);
            {
                std::lock_guard<std::mutex> lk(barrierMu);
                ++doneCount;
            }
            barrierCV.notify_all();
        });
    }

    // Wait for every submitted task to finish.
    {
        std::unique_lock<std::mutex> lk(barrierMu);
        barrierCV.wait(lk, [&] { return doneCount >= submitted; });
    }

    return batchResp;
}

SimulationBatchResponseDTO BatchExecutionEngine::executeMonteCarlo(
    const SimulationRequestDTO& baseRequest,
    const MonteCarloConfigDTO& config,
    RunSolver solver,
    ProgressFn progress,
    const std::atomic<bool>& cancelled,
    JobScheduler* scheduler)
{
    // Build batch request with deterministically-seeded per-sample variations.
    // Each sample uses an independent RNG seeded by (config.seed ^ sample_index)
    // so that sequential and parallel execution produce bit-identical results.
    SimulationBatchRequestDTO batchReq;
    batchReq.source       = baseRequest.source;
    batchReq.analysis     = baseRequest.analysis;
    batchReq.parameters   = baseRequest.parameters;
    batchReq.corner       = baseRequest.corner;
    batchReq.sessionToken = baseRequest.sessionToken;

    batchReq.runs.reserve(config.sampleCount);
    for (uint32_t i = 0; i < config.sampleCount; ++i) {
        // Per-sample seed: XOR with index makes each sample independent and
        // reproducible no matter whether threads execute in which order.
        acutesim::math::PortableRNG sampleRng(config.seed ^ static_cast<uint64_t>(i));
        BatchRunDTO run;
        run.runIndex  = i;
        run.overrides = generateVariations(config.variations, sampleRng);
        batchReq.runs.push_back(std::move(run));
    }

    // Route to parallel engine if a scheduler is available; else sequential.
    SimulationBatchResponseDTO result;
    if (scheduler) {
        result = executeBatchParallel(batchReq, solver, progress, cancelled, scheduler);
    } else {
        result = executeBatch(batchReq, solver, progress, cancelled);
    }

    computeStatistics(result);
    return result;
}

std::vector<ParameterVariationDTO> BatchExecutionEngine::generateVariations(
    const std::vector<MonteCarloVariationDTO>& variations,
    acutesim::math::PortableRNG& rng)
{
    std::vector<ParameterVariationDTO> overrides;
    overrides.reserve(variations.size());

    for (const auto& v : variations) {
        double sampledValue;

        if (v.distributionType == "Gaussian") {
            double sigma = v.nominalValue * v.tolerance;
            acutesim::math::PortableNormalDist dist(v.nominalValue, sigma);
            sampledValue = dist(rng);
            // Guard against non-physical values (e.g. negative resistance)
            if (sampledValue <= 0.0) sampledValue = v.nominalValue * 0.01;
        } else {
            // Uniform (default for all unrecognised distribution types)
            double lo = v.nominalValue * (1.0 - v.tolerance);
            double hi = v.nominalValue * (1.0 + v.tolerance);
            if (lo > hi) std::swap(lo, hi);
            acutesim::math::PortableUniformDist dist(lo, hi);
            sampledValue = dist(rng);
        }

        ParameterVariationDTO ov;
        ov.componentName = v.componentName;
        ov.propertyName  = v.propertyName;
        ov.overrideValue = sampledValue;
        overrides.push_back(ov);
    }

    return overrides;
}

void BatchExecutionEngine::computeStatistics(SimulationBatchResponseDTO& response)
{
    // Collect first (DC/peak) probe value from each successful run
    std::unordered_map<std::string, std::vector<double>> probeData;

    for (const auto& run : response.runResults) {
        if (!run.success) continue;
        for (const auto& kv : run.probeValues) {
            if (!kv.second.empty()) {
                probeData[kv.first].push_back(kv.second[0]);
            }
        }
    }

    for (const auto& kv : probeData) {
        const std::vector<double>& values = kv.second;
        if (values.empty()) continue;

        double sum = 0.0;
        double minV = values[0], maxV = values[0];
        for (double v : values) {
            sum += v;
            if (v < minV) minV = v;
            if (v > maxV) maxV = v;
        }
        double mean = sum / static_cast<double>(values.size());

        double sq_sum = 0.0;
        for (double v : values) {
            double d = v - mean;
            sq_sum += d * d;
        }
        // Sample stddev (N-1 denominator); 0 when only one run
        double stddev = (values.size() > 1)
            ? std::sqrt(sq_sum / static_cast<double>(values.size() - 1))
            : 0.0;

        response.statistics.mean[kv.first]   = mean;
        response.statistics.stddev[kv.first]  = stddev;
        response.statistics.minVal[kv.first]  = minV;
        response.statistics.maxVal[kv.first]  = maxV;
    }
}

} // namespace orchestration
} // namespace compute
} // namespace acutesim
