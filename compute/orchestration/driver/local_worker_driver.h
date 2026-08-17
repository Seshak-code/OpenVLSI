#pragma once
// ============================================================================
// local_worker_driver.h — In-Process Simulation Driver
// ============================================================================
// This is the ONLY class that directly includes CircuitSim and manages
// the solver lifecycle. It implements ISimulationDriver for local desktop
// and WASM execution.
//
// FUTURE: This class can be swapped with RemoteGrpcDriver, DockerizedDriver,
// or CloudSimulationDriver without changing a single line in the GUI.
// ============================================================================

#include "engine_api/isimulation_driver.h"
#include <memory>

namespace acutesim {
namespace compute {
namespace orchestration {

class LocalWorkerDriver : public ISimulationDriver {
public:
    LocalWorkerDriver();
    ~LocalWorkerDriver() override;

    // ── ISimulationDriver Interface ─────────────────────────────────────
    JobID submitJob(const SimulationRequestDTO& request) override;
    JobID submitBatchJob(const SimulationBatchRequestDTO& req) override;
    JobID submitStatisticalJob(const SimulationRequestDTO& baseReq, const MonteCarloConfigDTO& config) override;
    
    void  cancelJob(JobID id) override;
    bool  supportsConcurrentJobs() const override { return false; }

    // Phase 4 Reconcile: returns the live registry's type list — never stale.
    std::vector<AnalysisTypeDTO> supportedAnalyses() const override;

    // Terminal / Console
    TerminalResponseDTO executeCommand(const TerminalCommandDTO& cmd) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace orchestration
} // namespace compute
} // namespace acutesim
