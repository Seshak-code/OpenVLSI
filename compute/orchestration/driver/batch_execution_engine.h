#pragma once
// ============================================================================
// batch_execution_engine.h — Campaign Expansion + Monte Carlo RNG Engine
// ============================================================================
// Pure compute, no Qt, no threads. Owned by LocalWorkerDriver::Impl.
// Testable in isolation via the RunSolver callback interface.
//
// ARCHITECTURAL RULE: This class MUST NOT include physics/, gui/, or Qt
// headers. It operates entirely at the DTO boundary.
//
// Phase 4.8: Optional parallel execution via JobScheduler.
// Sequential path remains the default; parallel path activated by passing
// a non-null JobScheduler pointer.
// ============================================================================

#include "../dto/simulation_dto.h"
#include "acutesim_engine/math/portable_rng.h"
#include <functional>
#include <atomic>
#include <random>

// Forward-declare JobScheduler to keep this header Qt-free and thread-header-free
// for targets that do not need parallel execution.
namespace acutesim { namespace compute { namespace orchestration {
    class JobScheduler;
} } }

namespace acutesim {
namespace compute {
namespace orchestration {

class BatchExecutionEngine {
public:
    // RunSolver: provided by LocalWorkerDriver::Impl. Handles
    // override application, and actual solving. Returns a single run result.
    using RunSolver = std::function<SimulationResponseDTO(
        AnalysisTypeDTO analysis,
        const std::vector<ParameterVariationDTO>& overrides,
        const CornerParametersDTO& corner)>;

    using ProgressFn = std::function<void(uint32_t completed, uint32_t total)>;

    // Expand a batch request into runs, execute each via solver callback,
    // aggregate results. Sequential. Deterministic ordering by runIndex.
    SimulationBatchResponseDTO executeBatch(
        const SimulationBatchRequestDTO& request,
        RunSolver solver,
        ProgressFn progress,
        const std::atomic<bool>& cancelled);

    // Phase 4.8: Parallel batch execution via JobScheduler.
    // Submits each run as a task; waits for all to complete before returning.
    // Results are sorted by runIndex to guarantee deterministic ordering.
    // scheduler == nullptr falls back to sequential executeBatch.
    SimulationBatchResponseDTO executeBatchParallel(
        const SimulationBatchRequestDTO& request,
        RunSolver solver,
        ProgressFn progress,
        const std::atomic<bool>& cancelled,
        JobScheduler* scheduler);

    // Expand MC config into a batch request with seeded RNG variations,
    // execute via executeBatch or executeBatchParallel, then compute statistics.
    // scheduler != nullptr enables parallel execution via the thread pool.
    SimulationBatchResponseDTO executeMonteCarlo(
        const SimulationRequestDTO& baseRequest,
        const MonteCarloConfigDTO& config,
        RunSolver solver,
        ProgressFn progress,
        const std::atomic<bool>& cancelled,
        JobScheduler* scheduler = nullptr);

private:
    // Generate parameter overrides for one MC sample using seeded RNG.
    // Gaussian: normal_distribution(nominal, nominal * tolerance)
    // Uniform:  uniform_real_distribution(nominal*(1-tol), nominal*(1+tol))
    std::vector<ParameterVariationDTO> generateVariations(
        const std::vector<MonteCarloVariationDTO>& variations,
        acutesim::math::PortableRNG& rng);

    // Compute mean/stddev/min/max for each probe across all successful runs.
    void computeStatistics(SimulationBatchResponseDTO& response);
};

} // namespace orchestration
} // namespace compute
} // namespace acutesim
