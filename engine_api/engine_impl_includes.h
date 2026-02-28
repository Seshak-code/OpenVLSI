#pragma once

/**
 * engine_api/engine_impl_includes.h
 *
 * Aggregated public API for the AcuteSim simulation engine.
 * Link target: acutesim_engine_api (Qt-free, no gRPC, C++17)
 *
 * Phase 3.3 Hardened — paths updated for acutesim_engine/ extraction
 */

// --- Simulation Core ---
#include "acutesim_engine/physics/circuitsim.h"
#include "acutesim_engine/physics/device_physics.h"
#include "acutesim_engine/physics/quick_analysis.h"

// --- Circuit Data Model ---
#include "acutesim_engine/netlist/circuit.h"
#include "components/design_db.h"
#include "acutesim_engine/netlist/graph_canonical.h"

// --- Autodiff ---
#include "acutesim_engine/autodiff/dual.h"

// --- Compute Infrastructure ---
#include "acutesim_engine/infrastructure/netlist_compiler.h"
#include "acutesim_engine/infrastructure/postproc_engine.h"
#include "acutesim_engine/tensors/physics_tensors.h"
#include "acutesim_engine/math/linalg.h"

// --- Orchestration DTOs (Moved to engine_api) ---
#include "simulation_dto.h"
#include "engine_event_dto.h"

// --- Driver Interface (Moved to engine_api) ---
#include "isimulation_driver.h"

// --- Simulation Context (Moved to engine_api) ---
#include "simulation_context.h"

// --- Analysis Registry (Moved to engine_api) ---
#include "analysis_registry.h"

// --- Hardware Capability Detection ---
#include "acutesim_engine/infrastructure/hardware_capabilities.h"
