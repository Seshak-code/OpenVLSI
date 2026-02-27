// ============================================================================
// results_db.cpp — Implementation
// ============================================================================
#include "results_db.h"

#include <unordered_map>
#include <vector>
#include <shared_mutex>
#include <stdexcept>
#include <algorithm>

namespace acutesim {

using namespace compute::orchestration;

// ── Per-job storage record ────────────────────────────────────────────────────
struct JobResultRecord {
    std::vector<ResultChunk> chunks;     // Partial streaming chunks
    ResultHandle             result;     // Complete result (may be null during streaming)
    ResultStorageMode        mode   = ResultStorageMode::Memory;
    bool                     sealed = false;  // true after finalize()
};

// ── Chunk → WaveformTrace assembly ────────────────────────────────────────────
static std::vector<WaveformTrace>
assembleTraces(const JobResultRecord& rec) {
    std::vector<WaveformTrace> traces;
    if (rec.result) {
        const auto& r = *rec.result;
        for (const auto& [nodeIdx, voltages] : r.nodeVoltages) {
            WaveformTrace t;
            t.name            = "V(node" + std::to_string(nodeIdx) + ")";
            t.x               = r.timeVector;
            t.y               = voltages;
            t.sourceAnalysis  = AnalysisTypeDTO::TRANSIENT;
            traces.push_back(std::move(t));
        }
        for (const auto& [probeName, vals] : r.probeValues) {
            WaveformTrace t;
            t.name            = probeName;
            t.x               = r.timeVector;
            t.y               = vals;
            t.sourceAnalysis  = AnalysisTypeDTO::TRANSIENT;
            traces.push_back(std::move(t));
        }
        return traces;
    }

    // Assemble from streaming chunks
    std::unordered_map<int, WaveformTrace> nodeTraces;
    std::vector<double> timeAxis;
    for (const auto& chunk : rec.chunks) {
        for (double t : chunk.timeSlice) timeAxis.push_back(t);
        for (const auto& [nodeIdx, slice] : chunk.nodeVoltageSlice) {
            auto& tr = nodeTraces[nodeIdx];
            if (tr.name.empty()) {
                tr.name           = "V(node" + std::to_string(nodeIdx) + ")";
                tr.sourceAnalysis = AnalysisTypeDTO::TRANSIENT;
            }
            for (double v : slice) tr.y.push_back(v);
        }
    }
    for (auto& [idx, tr] : nodeTraces) {
        tr.x = timeAxis;
        traces.push_back(std::move(tr));
    }
    return traces;
}

// ── PIMPL ─────────────────────────────────────────────────────────────────────
struct ResultsDB::Impl {
    mutable std::shared_mutex               mutex_;
    std::unordered_map<JobID, JobResultRecord> records_;
    std::vector<IResultsObserver*>           observers_;
};

// ── Public API ────────────────────────────────────────────────────────────────
ResultsDB::ResultsDB() : impl_(std::make_unique<Impl>()) {}
ResultsDB::~ResultsDB() = default;

void ResultsDB::storeResult(JobID id, ResultHandle result,
                             ResultStorageMode mode)
{
    std::unique_lock lock(impl_->mutex_);
    auto& rec  = impl_->records_[id];
    if (rec.sealed) return;  // Never overwrite a finalized result
    rec.result = std::move(result);
    rec.mode   = mode;
    rec.sealed = true;   // Single-shot store is self-finalizing

    auto traces = assembleTraces(rec);
    lock.unlock();

    for (auto* obs : impl_->observers_)
        obs->onResultFinalized(id, traces);
}

void ResultsDB::appendPartialResult(JobID id, ResultChunk chunk) {
    std::unique_lock lock(impl_->mutex_);
    auto& rec = impl_->records_[id];
    if (rec.sealed) return;  // Append-only: silently drop after finalize
    rec.chunks.push_back(std::move(chunk));
    ResultChunk copy = rec.chunks.back();
    lock.unlock();

    for (auto* obs : impl_->observers_)
        obs->onChunkAvailable(copy);
}

void ResultsDB::finalize(JobID id) {
    std::vector<WaveformTrace> traces;
    {
        std::unique_lock lock(impl_->mutex_);
        auto it = impl_->records_.find(id);
        if (it == impl_->records_.end()) return;
        if (it->second.sealed) return;
        it->second.sealed = true;
        traces = assembleTraces(it->second);
    }
    for (auto* obs : impl_->observers_)
        obs->onResultFinalized(id, traces);
}

std::vector<WaveformTrace> ResultsDB::getTraces(JobID id) const {
    std::shared_lock lock(impl_->mutex_);
    auto it = impl_->records_.find(id);
    if (it == impl_->records_.end()) return {};
    return assembleTraces(it->second);
}

ResultHandle ResultsDB::getResult(JobID id) const {
    std::shared_lock lock(impl_->mutex_);
    auto it = impl_->records_.find(id);
    if (it == impl_->records_.end()) return nullptr;
    return it->second.result;
}

bool ResultsDB::isFinalized(JobID id) const {
    std::shared_lock lock(impl_->mutex_);
    auto it = impl_->records_.find(id);
    return it != impl_->records_.end() && it->second.sealed;
}

void ResultsDB::clear(JobID id) {
    std::unique_lock lock(impl_->mutex_);
    impl_->records_.erase(id);
}

void ResultsDB::clearAll() {
    std::unique_lock lock(impl_->mutex_);
    impl_->records_.clear();
}

void ResultsDB::addObserver(IResultsObserver* observer) {
    std::unique_lock lock(impl_->mutex_);
    impl_->observers_.push_back(observer);
}

void ResultsDB::removeObserver(IResultsObserver* observer) {
    std::unique_lock lock(impl_->mutex_);
    auto& obs = impl_->observers_;
    obs.erase(std::remove(obs.begin(), obs.end(), observer), obs.end());
}

} // namespace acutesim
