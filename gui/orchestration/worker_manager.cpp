#include "worker_manager.h"
#include <QTimer>
#include <QDebug>
#include <chrono>

namespace acutesim::gui::orchestration {

WorkerManager* WorkerManager::instance() {
    static WorkerManager s_instance;
    return &s_instance;
}

WorkerManager::WorkerManager(QObject* parent) : QObject(parent) {
    m_state.isConnected = false;
    m_state.workerId = "Disconnected";
    // Start polling loop
    m_running = true;
    m_pollThread = std::thread(&WorkerManager::pollStatus, this);
}

WorkerManager::~WorkerManager() {
    m_running = false;
    if (m_pollThread.joinable()) {
        m_pollThread.join();
    }
}

void WorkerManager::connectToWorker(const QString& target) { 
    // In our design, WorkerClient instantiates immediately, 
    // but the connection is logical.
    m_client = std::make_unique<acutesim::worker::WorkerClient>(target.toStdString());
    m_state.activeProvider = "Local (Docker)"; // Default for now
}

void WorkerManager::disconnect() {
    m_client.reset();
    m_state.isConnected = false;
    emit statusChanged(m_state);
}

void WorkerManager::pollStatus() {
    while (m_running) {
        if (!m_client) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        try {
            auto status = m_client->getStatus();
            m_state.workerId = QString::fromStdString(status.workerId);
            m_state.version = QString::fromStdString(status.version);
            m_state.isConnected = status.ready;
            m_state.cpuUsage = status.cpuUsage;
            m_state.capabilities.clear();
            for (const auto& tool : status.supportedTools) {
                m_state.capabilities << QString::fromStdString(tool);
            }
        } catch (const std::exception& e) {
             m_state.isConnected = false;
             // qDebug() << "Worker poll failed:" << e.what();
        }

        emit statusChanged(m_state);
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

} // namespace acutesim::gui::orchestration
