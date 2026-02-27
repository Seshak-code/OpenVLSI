#pragma once
// ============================================================================
// ijob_observer.h — Observer interface for SimulationJobManager events
// ============================================================================
// Pure abstract, no Qt, no filesystem. Implementors live in gui/ or ai_api/.
// All callbacks are called from the job manager's internal thread.
// GUI adapters must marshal to the main thread themselves.
// ============================================================================

#include "job_status.h"
#include <string>

namespace acutesim {

class IJobObserver {
public:
    virtual ~IJobObserver() = default;

    /// Called every time a job transitions to a new state.
    virtual void onJobStatusChanged(JobID id, JobStatus newStatus) = 0;

    /// Called periodically during Running/Streaming with [0.0, 1.0] progress.
    virtual void onJobProgress(JobID id, float progress,
                               const std::string& message) {}

    /// Called when a job transitions to Completed or Failed.
    virtual void onJobFinished(JobID id, JobStatus finalStatus) {}
};

} // namespace acutesim
