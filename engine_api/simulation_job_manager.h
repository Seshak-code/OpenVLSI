#pragma once
// ============================================================================
// simulation_job_manager.h — Central Job Bus for All 7 Connectivity Surfaces
// ============================================================================
// All simulation requests from ALL surfaces (waveform, terminal, schematic,
// AI, HDL, RTL→GDS) must pass through this manager.
//
// RULES:
//   - Every job has a owning ClientID — enables per-client cancellation
//   - Jobs respect priority order: Interactive > AISpeculative > BatchMonteCarlo
//   - State transitions are monotonic (see job_status.h)
//   - This header is PURE C++ / STL — no Qt, no filesystem assumptions
// ============================================================================

#include <cstdint>
#include "job_status.h"
#include "ijob_observer.h"
#include "engine_callbacks.h"
#include "simulation_dto.h"
#include <vector>
#include <memory>
#include <mutex>

namespace acutesim {

// ── Job Priority ─────────────────────────────────────────────────────────────
enum class JobPriority : uint8_t {
    Interactive    = 0,  // User-initiated (highest)
    AISpeculative  = 1,  // AI imagination / speculative runs
    BatchMonteCarlo = 2  // Background batch (lowest)
};

// ── Forward declarations ──────────────────────────────────────────────────────
namespace compute { namespace orchestration {
    struct SimulationRequestDTO;
    struct SimulationBatchRequestDTO;
    struct MonteCarloConfigDTO;
} }

// ── Job Descriptor ────────────────────────────────────────────────────────────
struct JobDescriptor {
    JobID       id;
    ClientID    owner;
    JobPriority priority;
    JobStatus   status;
};

// ── SimulationJobManager ──────────────────────────────────────────────────────
class SimulationJobManager {
public:
    SimulationJobManager();
    ~SimulationJobManager();

    // ── Submission API ─────────────────────────────────────────────────────
    // All surfaces submit through here — never directly to ISimulationDriver.
    JobID submit(
        ClientID                            owner,
        const compute::orchestration::SimulationRequestDTO& req,
        JobPriority                         priority = JobPriority::Interactive,
        const EngineCallbacks&              callbacks = {});

    JobID submitBatch(
        ClientID                                   owner,
        const compute::orchestration::SimulationBatchRequestDTO& req,
        JobPriority                                priority = JobPriority::BatchMonteCarlo,
        const EngineCallbacks&                     callbacks = {});

    JobID submitStatistical(
        ClientID                            owner,
        const compute::orchestration::SimulationRequestDTO& baseReq,
        const compute::orchestration::MonteCarloConfigDTO&  config,
        JobPriority                         priority = JobPriority::BatchMonteCarlo,
        const EngineCallbacks&              callbacks = {});

    // ── Lifecycle ──────────────────────────────────────────────────────────
    JobStatus               status(JobID id) const;
    JobDescriptor           descriptor(JobID id) const;
    void                    cancel(JobID id);
    void                    cancelOwner(ClientID owner);   // Kill all client's jobs
    void                    cancelAll();

    // ── Ownership Queries ──────────────────────────────────────────────────
    std::vector<JobID>      jobsOwnedBy(ClientID owner) const;
    std::vector<JobDescriptor> allJobs() const;

    // ── Observers ─────────────────────────────────────────────────────────
    void                    addObserver(IJobObserver* observer);
    void                    removeObserver(IJobObserver* observer);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace acutesim
