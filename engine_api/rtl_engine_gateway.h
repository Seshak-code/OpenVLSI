#pragma once
// ============================================================================
// rtl_engine_gateway.h — RTL Pipeline → Tensor MNA Engine Integration
// ============================================================================
// Handles post-synthesis simulation and parasitic back-annotation.
// TimingContext and AnalysisIntent are required for STA↔simulation integration.
// All paths are DTO-provided — no filesystem layout assumptions.
// ============================================================================

#include "simulation_dto.h"
#include "engine_callbacks.h"
#include "job_status.h"
#include <string>
#include <vector>
#include <cstdint>

namespace acutesim::engine_api {

// ── Timing context (required for clock-domain-aware post-synth sim) ───────────
struct TimingContext {
    std::vector<std::string> clockDomains;    // e.g. {"clk", "clk_fast"}
    double                   targetFreqHz = 0.0;
    std::string              processCorner;   // "tt", "ff", "ss", "sf", "fs"
    bool                     enableSTA = false;
};

// ── Analysis intent (drives simulation setup) ─────────────────────────────────
enum class AnalysisIntent : uint8_t {
    Functional,       // Behavioural correctness only
    TimingAware,      // Clock + setup/hold critical paths
    Power,            // Dynamic/leakage power estimation
    SignalIntegrity   // Cross-talk, IR drop
};

// ── Parasitic back-annotation from PEX (SPEF) ─────────────────────────────────
struct ParasiticBackAnnotation {
    std::string spefContent;      // Full SPEF text (DTO-provided, not a path)
    std::string topCellName;
};

// ── Post-synthesis simulation request ────────────────────────────────────────
struct PostSynthSimRequest {
    std::string     yosysJsonNetlist;   // Yosys output JSON (DTO content, not path)
    compute::orchestration::AnalysisTypeDTO analysis
        = compute::orchestration::AnalysisTypeDTO::DC;
    AnalysisIntent  intent   = AnalysisIntent::Functional;
    TimingContext   timing;
    bool            applyParasitics = false;
    std::string     spefContent;        // Optional SPEF for parasitic annotation
    EngineCallbacks callbacks;
};

// ── Result ────────────────────────────────────────────────────────────────────
struct PostSynthSimResult {
    bool        success = false;
    std::string errorMessage;
    JobID       jobId   = 0;
};

// ── Gateway ───────────────────────────────────────────────────────────────────
class RtlEngineGateway {
public:
    explicit RtlEngineGateway();
    ~RtlEngineGateway();

    /// Submit a post-synthesis simulation job.
    /// Returns immediately with a JobID; results arrive via ResultsDB + callbacks.
    PostSynthSimResult runPostSynthSim(const PostSynthSimRequest& request);

    /// Apply SPEF parasitics to a netlist JSON, returning an annotated netlist.
    bool applyParasitics(const ParasiticBackAnnotation& spef,
                         std::string& netlistJsonInOut);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace acutesim::engine_api
