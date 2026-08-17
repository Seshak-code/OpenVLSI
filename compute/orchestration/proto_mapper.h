#pragma once

#include "engine_api/simulation_dto.h"
#include "simulation_service.pb.h"

namespace acutesim::compute::orchestration::ProtoMapper {

// ── DTO -> Proto ────────────────────────────────────────────────────────────
acutesim::simulation::CornerParameters toProto(const CornerParametersDTO& dto);
acutesim::simulation::SimulationRequest toProto(const SimulationRequestDTO& dto);
acutesim::simulation::ParameterVariation toProto(const ParameterVariationDTO& dto);
acutesim::simulation::BatchRun toProto(const BatchRunDTO& dto);
acutesim::simulation::SimulationBatchRequest toProto(const SimulationBatchRequestDTO& dto);
acutesim::simulation::MonteCarloVariation toProto(const MonteCarloVariationDTO& dto);
acutesim::simulation::MonteCarloConfig toProto(const MonteCarloConfigDTO& dto);
acutesim::simulation::ConvergenceTelemetry toProto(const ConvergenceTelemetryDTO& dto);
acutesim::simulation::SimulationResult toProto(const SimulationResponseDTO& dto);
acutesim::simulation::StatisticalSummary toProto(const StatisticalSummaryDTO& dto);
acutesim::simulation::BatchSummary toProtoBatchSummary(const SimulationBatchResponseDTO& dto);

// ── Proto -> DTO ────────────────────────────────────────────────────────────
CornerParametersDTO fromProto(const acutesim::simulation::CornerParameters& msg);
SimulationRequestDTO fromProto(const acutesim::simulation::SimulationRequest& msg);
ParameterVariationDTO fromProto(const acutesim::simulation::ParameterVariation& msg);
BatchRunDTO fromProto(const acutesim::simulation::BatchRun& msg);
SimulationBatchRequestDTO fromProto(const acutesim::simulation::SimulationBatchRequest& msg);
MonteCarloVariationDTO fromProto(const acutesim::simulation::MonteCarloVariation& msg);
MonteCarloConfigDTO fromProto(const acutesim::simulation::MonteCarloConfig& msg);
ConvergenceTelemetryDTO fromProto(const acutesim::simulation::ConvergenceTelemetry& msg);
SimulationResponseDTO fromProto(const acutesim::simulation::SimulationResult& msg);
StatisticalSummaryDTO fromProto(const acutesim::simulation::StatisticalSummary& msg);
SimulationBatchResponseDTO fromProtoBatchSummary(const acutesim::simulation::BatchSummary& msg);

} // namespace acutesim::compute::orchestration::ProtoMapper
