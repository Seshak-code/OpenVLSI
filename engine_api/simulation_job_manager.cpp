// ============================================================================
// simulation_job_manager.cpp — Implementation
// ============================================================================
#include "simulation_job_manager.h"
#include "../compute/orchestration/driver/isimulation_driver.h"

#include <unordered_map>
#include <queue>
#include <vector>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <stdexcept>
#include <algorithm>

namespace acutesim {

// ── Internal Job Record ───────────────────────────────────────────────────────
struct JobRecord {
    JobDescriptor   desc;
    EngineCallbacks callbacks;
};

// ── Priority Comparator (lower enum value = higher priority) ──────────────────
struct JobOrder {
    bool operator()(const JobRecord& a, const JobRecord& b) const {
        return static_cast<uint8_t>(a.desc.priority) >
               static_cast<uint8_t>(b.desc.priority);
    }
};

// ── PIMPL ─────────────────────────────────────────────────────────────────────
struct SimulationJobManager::Impl {
    mutable std::shared_mutex           mutex_;
    std::atomic<uint64_t>               nextJobId_{1};
    std::unordered_map<JobID, JobRecord> jobs_;
    std::vector<IJobObserver*>          observers_;

    // ── ID generation ───────────────────────────────────────────────────────
    JobID generateId() {
        return nextJobId_.fetch_add(1, std::memory_order_relaxed);
    }

    // ── Register new job ────────────────────────────────────────────────────
    JobID registerJob(ClientID owner, JobPriority priority,
                      const EngineCallbacks& callbacks)
    {
        JobID id = generateId();
        JobRecord rec;
        rec.desc      = {id, owner, priority, JobStatus::Queued};
        rec.callbacks = callbacks;

        {
            std::unique_lock lock(mutex_);
            jobs_[id] = std::move(rec);
        }
        notifyStatusChanged(id, JobStatus::Queued);
        return id;
    }

    // ── State transition (enforces monotonic) ────────────────────────────────
    bool transition(JobID id, JobStatus newStatus) {
        std::unique_lock lock(mutex_);
        auto it = jobs_.find(id);
        if (it == jobs_.end()) return false;

        JobStatus current = it->second.desc.status;
        if (!isValidTransition(current, newStatus)) return false;

        it->second.desc.status = newStatus;
        lock.unlock();

        notifyStatusChanged(id, newStatus);
        if (isTerminal(newStatus)) {
            notifyFinished(id, newStatus);
        }
        return true;
    }

    // ── Observer dispatch ────────────────────────────────────────────────────
    void notifyStatusChanged(JobID id, JobStatus s) {
        std::shared_lock lock(mutex_);
        for (auto* obs : observers_) obs->onJobStatusChanged(id, s);
    }
    void notifyFinished(JobID id, JobStatus s) {
        std::shared_lock lock(mutex_);
        for (auto* obs : observers_) obs->onJobFinished(id, s);
    }
};

// ── SimulationJobManager public API ──────────────────────────────────────────
SimulationJobManager::SimulationJobManager()
    : impl_(std::make_unique<Impl>()) {}

SimulationJobManager::~SimulationJobManager() = default;

JobID SimulationJobManager::submit(
    ClientID owner,
    const compute::orchestration::SimulationRequestDTO& /*req*/,
    JobPriority priority,
    const EngineCallbacks& callbacks)
{
    JobID id = impl_->registerJob(owner, priority, callbacks);
    // TODO(Phase 2): Dispatch to ISimulationDriver queue with priority
    // For now, transition immediately to Queued (registered above)
    return id;
}

JobID SimulationJobManager::submitBatch(
    ClientID owner,
    const compute::orchestration::SimulationBatchRequestDTO& /*req*/,
    JobPriority priority,
    const EngineCallbacks& callbacks)
{
    return impl_->registerJob(owner, priority, callbacks);
}

JobID SimulationJobManager::submitStatistical(
    ClientID owner,
    const compute::orchestration::SimulationRequestDTO& /*baseReq*/,
    const compute::orchestration::MonteCarloConfigDTO&  /*config*/,
    JobPriority priority,
    const EngineCallbacks& callbacks)
{
    return impl_->registerJob(owner, priority, callbacks);
}

JobStatus SimulationJobManager::status(JobID id) const {
    std::shared_lock lock(impl_->mutex_);
    auto it = impl_->jobs_.find(id);
    if (it == impl_->jobs_.end()) return JobStatus::Failed;
    return it->second.desc.status;
}

JobDescriptor SimulationJobManager::descriptor(JobID id) const {
    std::shared_lock lock(impl_->mutex_);
    auto it = impl_->jobs_.find(id);
    if (it == impl_->jobs_.end())
        return {id, 0, JobPriority::Interactive, JobStatus::Failed};
    return it->second.desc;
}

void SimulationJobManager::cancel(JobID id) {
    impl_->transition(id, JobStatus::Cancelled);
}

void SimulationJobManager::cancelOwner(ClientID owner) {
    std::vector<JobID> owned;
    {
        std::shared_lock lock(impl_->mutex_);
        for (const auto& [id, rec] : impl_->jobs_) {
            if (rec.desc.owner == owner &&
                !isTerminal(rec.desc.status)) {
                owned.push_back(id);
            }
        }
    }
    for (JobID id : owned) impl_->transition(id, JobStatus::Cancelled);
}

void SimulationJobManager::cancelAll() {
    std::vector<JobID> active;
    {
        std::shared_lock lock(impl_->mutex_);
        for (const auto& [id, rec] : impl_->jobs_) {
            if (!isTerminal(rec.desc.status)) active.push_back(id);
        }
    }
    for (JobID id : active) impl_->transition(id, JobStatus::Cancelled);
}

std::vector<JobID> SimulationJobManager::jobsOwnedBy(ClientID owner) const {
    std::shared_lock lock(impl_->mutex_);
    std::vector<JobID> result;
    for (const auto& [id, rec] : impl_->jobs_) {
        if (rec.desc.owner == owner) result.push_back(id);
    }
    return result;
}

std::vector<JobDescriptor> SimulationJobManager::allJobs() const {
    std::shared_lock lock(impl_->mutex_);
    std::vector<JobDescriptor> result;
    result.reserve(impl_->jobs_.size());
    for (const auto& [id, rec] : impl_->jobs_) {
        result.push_back(rec.desc);
    }
    return result;
}

void SimulationJobManager::addObserver(IJobObserver* observer) {
    std::unique_lock lock(impl_->mutex_);
    impl_->observers_.push_back(observer);
}

void SimulationJobManager::removeObserver(IJobObserver* observer) {
    std::unique_lock lock(impl_->mutex_);
    auto& obs = impl_->observers_;
    obs.erase(std::remove(obs.begin(), obs.end(), observer), obs.end());
}

} // namespace acutesim
