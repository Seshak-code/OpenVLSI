#pragma once

/**
 * engine_api/engine.h
 *
 * Aggregated public API for the AcuteSim simulation engine.
 * Link target: acutesim_engine_api (Qt-free, no gRPC, C++17)
 *
 * Phase 3.3 Hardened
 */

// --- Simulation Core ---
#include "../physics/circuitsim.h"
#include "../physics/device_physics.h"
#include "../physics/quick_analysis.h"

// --- Circuit Data Model ---
#include "../components/circuit.h"
#include "../components/design_db.h"
#include "../components/graph_canonical.h"

// --- Autodiff ---
#include "../autodiff/dual.h"

// --- Compute Infrastructure ---
#include "../compute/infrastructure/netlist_compiler.h"
#include "../compute/infrastructure/postproc_engine.h"
#include "../compute/tensors/physics_tensors.h"
#include "../compute/math/linalg.h"

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
#include "../compute/hardware_capabilities.h"
