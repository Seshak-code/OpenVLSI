#pragma once
// ============================================================================
// proto_mapper.h — DTO ↔ Proto Conversion Contract
// ============================================================================
// Declares free functions that translate between the C++ simulation_dto.h
// types (used by all drivers and the GUI) and the proto message types
// generated from simulation_service.proto.
//
// RULE: These functions are only compiled under ACUTESIM_ENABLE_ORCHESTRATION.
//       The declarations here are always visible; bodies are in proto_mapper.cpp
//       which is guarded.
//
// DIRECTION:
//   toProto()   — DTO → proto  (client transmit, server receive and respond)
//   fromProto() — proto → DTO  (client receive, server receive)
//
// INVARIANT: Conversion is lossless for all fields present in both types.
//            Fields added in future DTO versions map to new proto field IDs
//            (never reuse IDs). If a proto field is missing (proto3 default),
//            the DTO default value is used — never a hard error.
// ============================================================================

#ifdef ACUTESIM_ENABLE_ORCHESTRATION

#include "../dto/simulation_dto.h"

// Forward-declare proto types to avoid including generated headers here
namespace acutesim { namespace simulation {
    class CornerParameters;
    class SimulationRequest;
    class SimulationBatchRequest;
    class StatisticalJobRequest;
    class SimulationResult;
    class BatchSummary;
    class ConvergenceTelemetry;
    class TimeSeries;
}}

namespace acutesim {
namespace compute {
namespace orchestration {

// ── CornerParametersDTO ──────────────────────────────────────────────────────

acutesim::simulation::CornerParameters
    toProto(const CornerParametersDTO& dto);

CornerParametersDTO
    fromProto(const acutesim::simulation::CornerParameters& msg);

// ── SimulationRequestDTO ────────────────────────────────────────────────────

acutesim::simulation::SimulationRequest
    toProto(const SimulationRequestDTO& dto);

SimulationRequestDTO
    fromProto(const acutesim::simulation::SimulationRequest& msg);

// ── SimulationBatchRequestDTO ───────────────────────────────────────────────

acutesim::simulation::SimulationBatchRequest
    toProto(const SimulationBatchRequestDTO& dto);

SimulationBatchRequestDTO
    fromProto(const acutesim::simulation::SimulationBatchRequest& msg);

// ── MonteCarloConfigDTO (embedded in StatisticalJobRequest) ─────────────────

// Client side: pack base request + MC config into the proto request
acutesim::simulation::StatisticalJobRequest
    toProto(const SimulationRequestDTO& base, const MonteCarloConfigDTO& mc);

// Server side: unpack both from the proto request
SimulationRequestDTO  baseRequestFromProto(
    const acutesim::simulation::StatisticalJobRequest& msg);
MonteCarloConfigDTO   mcConfigFromProto(
    const acutesim::simulation::StatisticalJobRequest& msg);

// ── SimulationResponseDTO ───────────────────────────────────────────────────
// These build the SimulationEvent wrapper directly (result + session_token + flags).

// Build a SimulationEvent carrying a run_result payload.
// is_finished = true only for single-run jobs; batch jobs set it separately.
acutesim::simulation::SimulationResult
    toProto(const SimulationResponseDTO& dto, const std::string& sessionToken);

SimulationResponseDTO
    fromProto(const acutesim::simulation::SimulationResult& msg);

// ── SimulationBatchResponseDTO (summary only) ───────────────────────────────

acutesim::simulation::BatchSummary
    toProto(const SimulationBatchResponseDTO& dto, const std::string& sessionToken);

// Reconstruct a full SimulationBatchResponseDTO from a BatchSummary + the
// already-accumulated per-run SimulationResult messages (client side).
SimulationBatchResponseDTO fromProto(
    const acutesim::simulation::BatchSummary& summary,
    std::vector<SimulationResponseDTO> accumulatedRuns);

// ── ConvergenceTelemetryDTO ─────────────────────────────────────────────────

acutesim::simulation::ConvergenceTelemetry
    toProto(const ConvergenceTelemetryDTO& dto);

ConvergenceTelemetryDTO
    fromProto(const acutesim::simulation::ConvergenceTelemetry& msg);

// ── AnalysisTypeDTO ──────────────────────────────────────────────────────────

acutesim::simulation::AnalysisType
    toProto(AnalysisTypeDTO dto);

AnalysisTypeDTO
    fromProto(acutesim::simulation::AnalysisType proto);

} // namespace orchestration
} // namespace compute
} // namespace acutesim

#endif // ACUTESIM_ENABLE_ORCHESTRATION
