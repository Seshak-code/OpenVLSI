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

    // Signal all outstanding jobs to stop and clean up cancel flags
    for (auto& [id, flag] : m_jobCancelFlags) {
        flag->store(true);
        delete flag;
    }
    m_jobCancelFlags.clear();
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
        }

        emit statusChanged(m_state);
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

QString WorkerManager::submitSynthesis(const acutesim::compute::orchestration::SynthesisConfig& config) {
    QString jobId = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);

    if (!m_client) {
        emit jobLogReceived(jobId, "No worker connected", true);
        emit jobCompleted(jobId, false, "");
        return jobId;
    }

    // Generate TCL script from config
    QString tclScript = acutesim::compute::orchestration::YosysTask::generateTcl(config);

    // Build gRPC config
    acutesim::worker::SynthesisJobConfig grpcConfig;
    grpcConfig.jobId = jobId.toStdString();
    grpcConfig.topModule = config.topModule.toStdString();
    grpcConfig.targetLibrary = config.targetLibrary.toStdString();
    grpcConfig.tclScript = tclScript.toStdString();

    for (const auto& src : config.sourceFiles) {
        grpcConfig.sourceFiles.push_back(src.toStdString());
    }

    auto* cancelFlag = new std::atomic<bool>(false);
    m_jobCancelFlags[jobId.toStdString()] = cancelFlag;

    // Capture by value for thread safety
    auto client = m_client.get();
    std::thread([this, grpcConfig, jobId, client, cancelFlag]() {
        client->runSynthesis(
            grpcConfig,
            [this, jobId, cancelFlag](const std::string& line, bool isError) {
                if (cancelFlag->load()) return;
                QMetaObject::invokeMethod(this, [this, jobId, line, isError]() {
                    emit jobLogReceived(jobId, QString::fromStdString(line), isError);
                }, Qt::QueuedConnection);
            },
            [this, jobId, cancelFlag](bool success, const std::string& artifactPath) {
                if (cancelFlag->load()) return;
                QMetaObject::invokeMethod(this, [this, jobId, success, artifactPath]() {
                    emit jobCompleted(jobId, success, QString::fromStdString(artifactPath));
                }, Qt::QueuedConnection);
            }
        );
    }).detach();

    return jobId;
}

QString WorkerManager::submitPlaceAndRoute(
    const acutesim::compute::orchestration::FloorplanConfig& fpConfig,
    const acutesim::compute::orchestration::PlacementConfig& placeConfig,
    const acutesim::compute::orchestration::CTSConfig& ctsConfig,
    const acutesim::compute::orchestration::RoutingConfig& routeConfig)
{
    QString jobId = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);

    if (!m_client) {
        emit jobLogReceived(jobId, "No worker connected", true);
        emit jobCompleted(jobId, false, "");
        return jobId;
    }

    // Generate full PnR TCL
    QString tclScript = acutesim::compute::orchestration::OpenRoadTask::generateFullFlowTcl(
        fpConfig, placeConfig, ctsConfig, routeConfig);

    // Build gRPC config
    acutesim::worker::PnRJobConfig grpcConfig;
    grpcConfig.jobId = jobId.toStdString();
    grpcConfig.tclScript = tclScript.toStdString();
    grpcConfig.dieWidth = fpConfig.dieArea.width();
    grpcConfig.dieHeight = fpConfig.dieArea.height();
    grpcConfig.coreWidth = fpConfig.dieArea.width() * fpConfig.coreUtilization;
    grpcConfig.coreHeight = fpConfig.dieArea.height() * fpConfig.coreUtilization;

    auto* cancelFlag = new std::atomic<bool>(false);
    m_jobCancelFlags[jobId.toStdString()] = cancelFlag;

    auto client = m_client.get();
    std::thread([this, grpcConfig, jobId, client, cancelFlag]() {
        client->runPlaceAndRoute(
            grpcConfig,
            [this, jobId, cancelFlag](const std::string& line, bool isError) {
                if (cancelFlag->load()) return;
                QMetaObject::invokeMethod(this, [this, jobId, line, isError]() {
                    emit jobLogReceived(jobId, QString::fromStdString(line), isError);
                }, Qt::QueuedConnection);
            },
            [this, jobId, cancelFlag](float progress) {
                if (cancelFlag->load()) return;
                QMetaObject::invokeMethod(this, [this, jobId, progress]() {
                    emit jobProgress(jobId, progress);
                }, Qt::QueuedConnection);
            },
            [this, jobId, cancelFlag](bool success, const std::string& artifactPath) {
                if (cancelFlag->load()) return;
                QMetaObject::invokeMethod(this, [this, jobId, success, artifactPath]() {
                    emit jobCompleted(jobId, success, QString::fromStdString(artifactPath));
                }, Qt::QueuedConnection);
            }
        );
    }).detach();

    return jobId;
}

QString WorkerManager::submitSignOff(const acutesim::compute::orchestration::DRCConfig& drcConfig) {
    QString jobId = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);

    if (!m_client) {
        emit jobLogReceived(jobId, "No worker connected", true);
        emit jobCompleted(jobId, false, "");
        return jobId;
    }

    // Generate DRC script
    QString drcScript = acutesim::compute::orchestration::KLayoutTask::generateDRCScript(drcConfig);

    // Build gRPC config
    acutesim::worker::SignOffJobConfig grpcConfig;
    grpcConfig.jobId = jobId.toStdString();
    grpcConfig.layoutFile = drcConfig.layoutFile.toStdString();
    grpcConfig.scriptContent = drcScript.toStdString();
    grpcConfig.checkType = "DRC";

    auto* cancelFlag = new std::atomic<bool>(false);
    m_jobCancelFlags[jobId.toStdString()] = cancelFlag;

    auto client = m_client.get();
    std::thread([this, grpcConfig, jobId, client, cancelFlag]() {
        client->runSignOff(
            grpcConfig,
            [this, jobId, cancelFlag](const std::string& line, bool isError) {
                if (cancelFlag->load()) return;
                QMetaObject::invokeMethod(this, [this, jobId, line, isError]() {
                    emit jobLogReceived(jobId, QString::fromStdString(line), isError);
                }, Qt::QueuedConnection);
            },
            [this, jobId, cancelFlag](bool success, const std::string& artifactPath) {
                if (cancelFlag->load()) return;
                QMetaObject::invokeMethod(this, [this, jobId, success, artifactPath]() {
                    emit jobCompleted(jobId, success, QString::fromStdString(artifactPath));
                }, Qt::QueuedConnection);
            }
        );
    }).detach();

    return jobId;
}

void WorkerManager::cancelJob(const QString& jobId) {
    auto it = m_jobCancelFlags.find(jobId.toStdString());
    if (it != m_jobCancelFlags.end()) {
        it->second->store(true);
        emit jobLogReceived(jobId, "Job cancelled by user", false);
        emit jobCompleted(jobId, false, "");
    }
}

} // namespace acutesim::gui::orchestration
