#pragma once
#include "simulation_dto.h"
#include "engine_callbacks.h"

namespace acutesim {

/**
 * engine_api/isimulation_engine.h
 *
 * Top-level abstract interface for the AcuteSim simulation engine.
 * This is the ONLY public API contract exposed to the GUI layer.
 *
 * Deliberately minimal: POD DTOs + callbacks only. No Qt, no gRPC,
 * no implementation headers.
 */
class ISimulationEngine {
public:
    virtual acutesim::compute::orchestration::SimulationResponseDTO run(
        const acutesim::compute::orchestration::SimulationRequestDTO&,
        const EngineCallbacks&) = 0;
    virtual ~ISimulationEngine() = default;
};

} // namespace acutesim
