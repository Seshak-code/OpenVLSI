#pragma once
// ============================================================================
// simulation_driver_adapter.h — Qt Signal Adapter for ISimulationDriver
// ============================================================================
// ARCHITECTURAL ROLE: This is the ONLY file that bridges the GUI-agnostic
// ISimulationDriver callbacks (std::function) to Qt signals (QObject).
// It lives inside gui/ because it is a GUI concern.
//
// The adapter:
// 1. Owns an ISimulationDriver (via unique_ptr for polymorphism)
// 2. Wires the driver's std::function callbacks to Qt signals
// 3. Ensures callbacks are marshalled to the GUI thread via QMetaObject::invokeMethod
// ============================================================================

#include <QObject>
#include <QJsonObject>
#include <memory>
#include "engine_api/isimulation_driver.h"
#include "engine_api/simulation_dto.h"
#include "engine_api/engine_event_dto.h"

namespace acutesim {
namespace gui {
namespace analysis {

class SimulationDriverAdapter : public QObject {
    Q_OBJECT

public:
    using JobID = acutesim::compute::orchestration::JobID;
    using SimulationRequestDTO = acutesim::compute::orchestration::SimulationRequestDTO;
    using SimulationResponseDTO = acutesim::compute::orchestration::SimulationResponseDTO;
    using SimulationBatchRequestDTO = acutesim::compute::orchestration::SimulationBatchRequestDTO;
    using SimulationBatchResponseDTO = acutesim::compute::orchestration::SimulationBatchResponseDTO;
    using MonteCarloConfigDTO = acutesim::compute::orchestration::MonteCarloConfigDTO;

    explicit SimulationDriverAdapter(
        std::unique_ptr<acutesim::compute::orchestration::ISimulationDriver> driver,
        QObject* parent = nullptr)
        : QObject(parent)
        , driver_(std::move(driver))
    {
        // Wire driver callbacks -> Qt signals (thread-safe via QMetaObject)
        driver_->onJobCompleted = [this](JobID id, const SimulationResponseDTO& resp) {
            // Marshal to GUI thread
            QMetaObject::invokeMethod(this, [this, id, resp]() {
                emit jobCompleted(id, resp);
            }, Qt::QueuedConnection);
        };

        driver_->onJobProgress = [this](JobID id, float pct, const std::string& msg) {
            QMetaObject::invokeMethod(this, [this, id, pct, msg]() {
                emit jobProgress(id, pct, QString::fromStdString(msg));
            }, Qt::QueuedConnection);
        };
        
        driver_->onBatchCompleted = [this](JobID id, const SimulationBatchResponseDTO& resp) {
            QMetaObject::invokeMethod(this, [this, id, resp]() {
                emit batchCompleted(id, resp);
            }, Qt::QueuedConnection);
        };

        driver_->onBatchProgress = [this](JobID id, uint32_t completedRuns, uint32_t totalRuns) {
            QMetaObject::invokeMethod(this, [this, id, completedRuns, totalRuns]() {
                emit batchProgress(id, completedRuns, totalRuns);
            }, Qt::QueuedConnection);
        };

        driver_->onEngineEvent = [this](const acutesim::compute::orchestration::EngineEventDTO& event) {
            QMetaObject::invokeMethod(this, [this, event]() {
                emit engineEvent(event);
            }, Qt::QueuedConnection);
        };
    }

    // ── Submit / Cancel (delegates to driver) ───────────────────────────
    JobID submitJob(const SimulationRequestDTO& req) {
        return driver_->submitJob(req);
    }
    
    JobID submitBatchJob(const SimulationBatchRequestDTO& req) {
        return driver_->submitBatchJob(req);
    }
    
    JobID submitStatisticalJob(const SimulationRequestDTO& baseReq, const MonteCarloConfigDTO& config) {
        return driver_->submitStatisticalJob(baseReq, config);
    }

    void cancelJob(JobID id) {
        driver_->cancelJob(id);
    }

    acutesim::compute::orchestration::TerminalResponseDTO
    executeCommand(const acutesim::compute::orchestration::TerminalCommandDTO& cmd) {
        return driver_->executeCommand(cmd);
    }

    // ── Query ───────────────────────────────────────────────────────────
    bool supportsAnalysis(acutesim::compute::orchestration::AnalysisTypeDTO type) const {
        return driver_->supportsAnalysis(type);
    }

signals:
    void jobCompleted(quint64 jobId, const acutesim::compute::orchestration::SimulationResponseDTO& response);
    void jobProgress(quint64 jobId, float percent, const QString& message);
    
    void batchCompleted(quint64 jobId, const acutesim::compute::orchestration::SimulationBatchResponseDTO& response);
    void batchProgress(quint64 jobId, quint32 completedRuns, quint32 totalRuns);

    void engineEvent(const acutesim::compute::orchestration::EngineEventDTO& event);

private:
    std::unique_ptr<acutesim::compute::orchestration::ISimulationDriver> driver_;
};

} // namespace analysis
} // namespace gui
} // namespace acutesim
