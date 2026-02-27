#pragma once
// ============================================================================
// engine_connector.h — AI API → Engine Connectivity Adapter
// ============================================================================
// Forces all AI simulation requests through engine_api — the engine remains
// a black box to AI layers. Returning JobID makes every AI job independently
// cancellable, preventing runaway speculative simulation.
// ============================================================================

#include "../../engine_api/simulation_job_manager.h"
#include "../../engine_api/simulation_dto.h"
#include "../../engine_api/engine_callbacks.h"

namespace acutesim::ai_api::simulation {

// ── AI-side view of a circuit state (from completed simulation) ───────────────
struct CircuitState {
    std::unordered_map<int, double>          dcOperatingPoint; // node → voltage
    std::unordered_map<std::string, double>  probeMeasures;    // probe → value
    bool                                     converged = false;
};

// ── AI Engine Connector ───────────────────────────────────────────────────────
class AiSimulationConnector {
public:
    /// @param mgr       The shared SimulationJobManager (platform-wide singleton).
    /// @param clientId  Unique ClientID for this AI session — enables cancelOwner().
    AiSimulationConnector(acutesim::SimulationJobManager* mgr,
                          acutesim::ClientID              clientId);
    ~AiSimulationConnector();

    /// Submit a speculative AI simulation. Returns immediately with a JobID.
    /// Caller is responsible for cancellation to prevent runaway.
    acutesim::JobID runSpeculative(
        const acutesim::compute::orchestration::SimulationRequestDTO& req,
        acutesim::JobPriority      priority  = acutesim::JobPriority::AISpeculative,
        const acutesim::EngineCallbacks& cbs = {});

    /// Cancel ALL jobs submitted by this connector's clientId.
    void cancelAll();

    /// Build a CircuitState from a completed SimulationResponseDTO for AI use.
    CircuitState buildCircuitState(
        const acutesim::compute::orchestration::SimulationResponseDTO& resp) const;

    acutesim::ClientID clientId() const { return clientId_; }

private:
    acutesim::SimulationJobManager* mgr_;
    acutesim::ClientID              clientId_;
};

} // namespace acutesim::ai_api::simulation
