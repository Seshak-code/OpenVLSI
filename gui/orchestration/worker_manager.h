#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <memory>
#include <thread>
#include <atomic>

#include "compute/orchestration/grpc_worker_client.h"

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
 * Manages the connection loop and exposes status via Signals.
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

signals:
    void statusChanged(const WorkerState& state);
    void logReceived(const QString& msg, bool isError);
    void jobFinished(bool success, const QString& resultPath);

private:
    void pollStatus(); // Periodic health check

    std::unique_ptr<acutesim::worker::WorkerClient> m_client;
    WorkerState m_state;
    std::atomic<bool> m_running { false };
    std::thread m_pollThread;
};

} // namespace acutesim::gui::orchestration
