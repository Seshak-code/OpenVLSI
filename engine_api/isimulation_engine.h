#pragma once
// ============================================================================
// isimulation_engine.h — Engine Public API Contract (v2)
// ============================================================================
// This is the SOLE public entry point into libacutesim_engine.
// All simulation, GPU, precision, and session lifecycle is managed here.
//
// RULES:
//   ✅ Only POD DTOs and std types in this header
//   ❌ No Qt, No gRPC, No Dawn/WebGPU types
//   ❌ No #include of acutesim_engine/ internals (engine_api must stay pure)
//   ❌ No virtual methods below the session boundary (hot path must stay inline)
// ============================================================================

#include "simulation_dto.h"
#include "engine_callbacks.h"
#include "job_status.h"

#include <memory>
#include <string>
#include <cstddef>

namespace acutesim {

using namespace acutesim::compute::orchestration;

// ── Precision control ──────────────────────────────────────────────────────
enum class PrecisionMode : uint8_t {
    Native_F64,     // IEEE 754 double — CPU scalar paths
    Emulated_F64,   // Hi-lo pair splitting — GPU determinism paths
    Mixed           // F64 for NR outer loop, EmF64 for GPU kernels (default)
};

// ── Engine capabilities descriptor ────────────────────────────────────────
// Clients query this instead of probing internals.
// Populated at engine initialization from hardware detection.
struct ENGINE_API EngineCapabilities {
    bool        gpuAvailable          = false;
    bool        emulatedF64Supported  = true;    // Always true (software impl)
    bool        batchedSolveSupported = false;
    bool        avx512Supported       = false;
    bool        neonSupported         = false;
    size_t      maxBatchSize          = 1;
    size_t      maxParallelSessions   = 1;
    std::string gpuAdapterName;
    std::string engineVersionString;
};

// Forward declarations
class SimulationSession;

// ── Engine interface ───────────────────────────────────────────────────────
// Only the OUTER boundary is virtual.
// Internal solver loops are NEVER virtual — they are compile-time bound
// inline/templated functions inside acutesim_engine/solvers/.
class ENGINE_API ISimulationEngine {
public:
    virtual ~ISimulationEngine() = default;

    // === Primary session factory ===
    // Each session is an isolated simulation context.
    // Enables: concurrent simulations, AI speculative isolation, GPU batching.
    // Sessions are independent — cancel one without affecting others.
    virtual std::unique_ptr<SimulationSession> createSession() = 0;

    // === Capability and configuration ===
    virtual EngineCapabilities capabilities() const = 0;
    virtual void setPrecisionMode(PrecisionMode mode) = 0;
    virtual PrecisionMode precisionMode() const = 0;

    // === Factory function (defined in acutesim_engine/engine_impl.cpp) ===
    // The ONLY legal way to obtain an engine instance from outside.
    static ENGINE_API std::unique_ptr<ISimulationEngine> create();
};

} // namespace acutesim

// ENGINE_API visibility macro (provided by engine, consumed by clients)
#ifndef ENGINE_API
  #define ENGINE_API
#endif
