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
#include <mutex>

#include "compute/orchestration/grpc_worker_client.h"
#include "compute/orchestration/drivers/rtl_flow.h"

namespace acutesim::gui::orchestration {

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

    WorkerState currentState() const { std::lock_guard<std::mutex> l(m_mutex); return m_state; }
    bool isConnected() const { std::lock_guard<std::mutex> l(m_mutex); return m_state.isConnected; }

    // RTL -> GDS (Roadmap Phase 6) runs locally, on a background thread: OpenROAD-flow-scripts,
    // then the PDK's DRC and LVS decks. Progress 0..1 arrives as jobProgress; the results as
    // synthesis/pnr/drc/lvsResultReady, then jobCompleted with the GDS path.
    bool canRunRtlFlow() const;
    QString rtlFlowMissing() const;  // tools not found, empty when the flow can run
    QString submitRtlToGds(const acutesim::compute::orchestration::RtlFlowConfig& config);
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

    // The poll thread reads both while the GUI thread connects or disconnects: guarded by m_mutex; a poll holds its
    // own reference to the client, so a disconnect mid-call cannot free it
    mutable std::mutex m_mutex;
    std::shared_ptr<acutesim::worker::WorkerClient> m_client;
    WorkerState m_state;
    ToolInventory m_inventory;
    std::atomic<bool> m_running { false };
#ifndef EMSCRIPTEN
    std::thread m_pollThread;
#endif

    // Cancel flags of running flows (shared with their threads)
    std::map<std::string, std::shared_ptr<std::atomic<bool>>> m_jobCancelFlags;
};

} // namespace acutesim::gui::orchestration
