#pragma once
// ============================================================================
// results_db.h — Append-Only, Immutable Simulation Results Bus
// ============================================================================
// ARCHITECTURAL RULES:
//   1. Results are NEVER overwritten once stored. Append-only.
//   2. Results are immutable: use shared_ptr<const> (ResultHandle).
//   3. Finalize() seals a job — no further appends accepted.
//   4. Thread-safe for concurrent observers (streaming Monte Carlo, AI loops).
//   5. No Qt. No filesystem paths. No GUI types.
// ============================================================================

#include "job_status.h"           // JobID
#include "simulation_dto.h"       // SimulationResponseDTO, AnalysisTypeDTO
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <functional>

namespace acutesim {

// Forward declaration
namespace compute { namespace orchestration { struct SimulationResponseDTO; } }

// ── Immutable result handle ───────────────────────────────────────────────────
using ResultHandle = std::shared_ptr<const compute::orchestration::SimulationResponseDTO>;

// ── Result storage mode ───────────────────────────────────────────────────────
enum class ResultStorageMode : uint8_t {
    Memory,       // Default: all data kept in RAM
    MemoryMapped, // Long transient sims, RTL waveform dumps
    Streaming     // Remote workers — data piped directly to observers
};

// ── Streaming / partial result chunk ─────────────────────────────────────────
struct ResultChunk {
    JobID       jobId;
    uint32_t    runIndex   = 0;   // For batch/MC — which run this belongs to
    uint32_t    chunkIndex = 0;   // Sequential, starting at 0
    std::vector<double> timeSlice;
    // node index → voltage slice (same length as timeSlice)
    std::unordered_map<int, std::vector<double>> nodeVoltageSlice;
    std::unordered_map<int, std::vector<double>> branchCurrentSlice;
};

// ── Waveform trace (engine-agnostic) ─────────────────────────────────────────
struct WaveformTrace {
    std::string          name;           // e.g. "V(out)", "I(R1)"
    std::vector<double>  x;             // time axis
    std::vector<double>  y;             // signal axis
    compute::orchestration::AnalysisTypeDTO sourceAnalysis;
};

// ── Observer for live streaming ───────────────────────────────────────────────
class IResultsObserver {
public:
    virtual ~IResultsObserver() = default;
    virtual void onChunkAvailable(const ResultChunk&) {}
    virtual void onResultFinalized(JobID, const std::vector<WaveformTrace>&) {}
};

// ── ResultsDB ─────────────────────────────────────────────────────────────────
class ResultsDB {
public:
    ResultsDB();
    ~ResultsDB();

    // ── Store complete result (e.g. DC, single TRAN) ─────────────────────
    void storeResult(JobID id, ResultHandle result,
                     ResultStorageMode mode = ResultStorageMode::Memory);

    // ── Append partial chunk (streaming TRAN, Monte Carlo batches) ────────
    void appendPartialResult(JobID id, ResultChunk chunk);

    // ── Finalize — seal the job, no further appends, notify observers ─────
    void finalize(JobID id);

    // ── Query ─────────────────────────────────────────────────────────────
    std::vector<WaveformTrace>  getTraces(JobID id) const;
    ResultHandle                getResult(JobID id) const;    // nullptr if streaming
    bool                        isFinalized(JobID id) const;

    // ── Lifecycle ─────────────────────────────────────────────────────────
    void clear(JobID id);
    void clearAll();

    // ── Observers ─────────────────────────────────────────────────────────
    void addObserver(IResultsObserver* observer);
    void removeObserver(IResultsObserver* observer);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace acutesim
