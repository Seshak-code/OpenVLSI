#pragma once

#include <QObject>
#include <QString>
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

namespace acutesim::gui::orchestration {

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

    WorkerState currentState() const { return m_state; }
    bool isConnected() const { return m_state.isConnected; }

    // Job submission - returns job ID (UUID string)
    QString submitSynthesis(const acutesim::compute::orchestration::SynthesisConfig& config);
    QString submitPlaceAndRoute(const acutesim::compute::orchestration::FloorplanConfig& fpConfig,
                                const acutesim::compute::orchestration::PlacementConfig& placeConfig,
                                const acutesim::compute::orchestration::CTSConfig& ctsConfig,
                                const acutesim::compute::orchestration::RoutingConfig& routeConfig);
    QString submitSignOff(const acutesim::compute::orchestration::DRCConfig& drcConfig);
    void cancelJob(const QString& jobId);

signals:
    void statusChanged(const WorkerState& state);
    void logReceived(const QString& msg, bool isError);
    void jobFinished(bool success, const QString& resultPath);

    // Extended job signals
    void jobProgress(const QString& jobId, float progress);
    void jobLogReceived(const QString& jobId, const QString& line, bool isError);
    void jobCompleted(const QString& jobId, bool success, const QString& artifactPath);

private:
    void pollStatus(); // Periodic health check

    std::unique_ptr<acutesim::worker::WorkerClient> m_client;
    WorkerState m_state;
    std::atomic<bool> m_running { false };
    std::thread m_pollThread;

    // Active jobs
    std::map<std::string, std::atomic<bool>*> m_jobCancelFlags;
};

} // namespace acutesim::gui::orchestration
