#pragma once
// ============================================================================
// schematic_sim_request.h — Schematic → Engine Simulation Request
// ============================================================================
// No Qt types. The schematic graph is serialised to a DTO before crossing
// the engine_api boundary.
// Split hashes enable granular incremental reuse:
//   - same topologyHash  → engine reuses compiled TensorNetlist
//   - same parameterHash → engine reuses compiled tensors
//   - different either   → full recompile
// ============================================================================

#include "simulation_dto.h"
#include "engine_callbacks.h"
#include <string>
#include <unordered_map>
#include <cstdint>

namespace acutesim::engine_api {

struct SchematicSimRequest {
    // Netlist source (JSON serialised by SchematicEngineConnector)
    std::string netlistJson;

    // Analysis to run
    compute::orchestration::AnalysisTypeDTO analysis
        = compute::orchestration::AnalysisTypeDTO::DC;

    // Key-value parameter overrides ("stopTime" → "100e-9")
    std::unordered_map<std::string, std::string> parameters;

    // ── Incremental simulation ─────────────────────────────────────────────
    bool     incremental    = false;
    uint64_t topologyHash   = 0;   // Hash of component connectivity
    uint64_t parameterHash  = 0;   // Hash of component values / temp / corner

    // ── Lifecycle callbacks ────────────────────────────────────────────────
    acutesim::EngineCallbacks callbacks;

    // ── Origin metadata (for ResultsDB labelling) ─────────────────────────
    std::string originDescription; // e.g. "Inverter schematic — DC sweep"
};

} // namespace acutesim::engine_api
