#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QUuid>
#include <QMap>
#include <memory>
#include <thread>
#include <atomic>
#include <map>

#include "compute/orchestration/grpc_worker_client.h"
#include "compute/orchestration/drivers/yosys_task.h"
#include "compute/orchestration/drivers/openroad_task.h"
#include "compute/orchestration/drivers/klayout_task.h"
#include "compute/orchestration/drivers/tool_driver.h"

namespace acutesim::gui::orchestration {

enum class ExecutionMode { LOCAL, REMOTE, AUTO };

/** Detected local EDA tools and PDKs. Empty on WASM. */
struct ToolInventory {
    bool hasYosys      = false;
    bool hasOpenROAD   = false;
    bool hasKLayout    = false;
    bool hasDocker     = false;
    QString yosysVersion;
    QString openRoadVersion;
    QString klayoutVersion;
    QStringList installedPDKs;  // PDK names found under $PDK_ROOT or common paths
    QString pdkRoot;            // Resolved PDK_ROOT path
};

struct WorkerState {
    QString workerId;
    QString version;
    bool isConnected = false;
    double cpuUsage = 0.0;
    QStringList capabilities;
    QString activeProvider; // "Local (Docker)", "Cloud", "WSL2"
};

/**
 * @brief Qt integration layer for the gRPC WorkerClient.
 * Manages the connection loop, job submission, and exposes status via Signals.
 */
class WorkerManager : public QObject {
    Q_OBJECT

public:
    explicit WorkerManager(QObject* parent = nullptr);
    ~WorkerManager();

    // Singleton accessor for simple global access (optional, but convenient for toolbar)
    static WorkerManager* instance();

    void connectToWorker(const QString& target = "localhost:50051");
    void disconnect();

    // Tool detection (async on native; returns empty inventory on WASM immediately)
    void detectLocalTools();
    ToolInventory toolInventory() const { return m_inventory; }

    WorkerState currentState() const { return m_state; }
    bool isConnected() const { return m_state.isConnected; }

    void setExecutionMode(ExecutionMode mode);
    ExecutionMode executionMode() const { return m_executionMode; }
    void setMockMode(bool enabled); // Proxy to ToolDriver
    bool canRunLocally() const;

    // Job submission - returns job ID (UUID string)
    QString submitSynthesis(const acutesim::compute::orchestration::SynthesisConfig& config);
    QString submitPlaceAndRoute(const acutesim::compute::orchestration::FloorplanConfig& fpConfig,
                                const acutesim::compute::orchestration::PlacementConfig& placeConfig,
                                const acutesim::compute::orchestration::CTSConfig& ctsConfig,
                                const acutesim::compute::orchestration::RoutingConfig& routeConfig);
    QString submitSignOff(const acutesim::compute::orchestration::DRCConfig& drcConfig);
    void cancelJob(const QString& jobId);

signals:
    void toolInventoryReady(const ToolInventory& inventory);
    void statusChanged(const WorkerState& state);
    void logReceived(const QString& msg, bool isError);
    void jobFinished(bool success, const QString& resultPath);

    // Extended job signals
    void jobProgress(const QString& jobId, float progress);
    void jobLogReceived(const QString& jobId, const QString& line, bool isError);
    void jobCompleted(const QString& jobId, bool success, const QString& artifactPath);

    // Structured result signals (emitted after parsing tool output)
    void synthesisResultReady(const QString& jobId, const acutesim::compute::orchestration::SynthesisResult& result);
    void pnrResultReady(const QString& jobId, const acutesim::compute::orchestration::PnRResult& result);
    void drcResultReady(const QString& jobId, const acutesim::compute::orchestration::DRCResult& result);
    void lvsResultReady(const QString& jobId, const acutesim::compute::orchestration::LVSResult& result);

private:
    void pollStatus(); // Periodic health check

    std::unique_ptr<acutesim::worker::WorkerClient> m_client;
    WorkerState m_state;
    ToolInventory m_inventory;
    ExecutionMode m_executionMode = ExecutionMode::AUTO;
    std::atomic<bool> m_running { false };
#ifndef EMSCRIPTEN
    std::thread m_pollThread;
#endif

    // Active jobs
    std::map<std::string, std::atomic<bool>*> m_jobCancelFlags;

    // Local execution helpers
    bool shouldRunLocally() const;
    void runLocalSynthesis(const QString& jobId, const acutesim::compute::orchestration::SynthesisConfig& config);
    void runLocalPnR(const QString& jobId,
                     const acutesim::compute::orchestration::FloorplanConfig& fpConfig,
                     const acutesim::compute::orchestration::PlacementConfig& placeConfig,
                     const acutesim::compute::orchestration::CTSConfig& ctsConfig,
                     const acutesim::compute::orchestration::RoutingConfig& routeConfig);
    void runLocalSignOff(const QString& jobId, const acutesim::compute::orchestration::DRCConfig& config);
};

} // namespace acutesim::gui::orchestration
