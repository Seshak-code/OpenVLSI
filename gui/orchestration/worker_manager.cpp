#include "worker_manager.h"
#include <QTimer>
#include <QDebug>
#include <QDir>
#ifdef EMSCRIPTEN
// QProcess not supported on WASM
#else
#include <QProcess>
#endif
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
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
#ifdef EMSCRIPTEN
    m_running = true;
    QTimer::singleShot(0, this, &WorkerManager::pollStatus);
#else
    m_running = true;
    m_pollThread = std::thread(&WorkerManager::pollStatus, this);
#endif
}

WorkerManager::~WorkerManager() {
    m_running = false;
#ifndef EMSCRIPTEN
    if (m_pollThread.joinable()) {
        m_pollThread.join();
    }
#endif

    // Running flows stop: their threads hold the flags and see them set
    for (auto& [id, flag] : m_jobCancelFlags) flag->store(true);
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
#ifdef EMSCRIPTEN
    if (!m_running) return;
    
    if (m_client) {
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
    }
    
    QTimer::singleShot(2000, this, &WorkerManager::pollStatus);
#else
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
#endif
}

bool WorkerManager::canRunRtlFlow() const {
    return rtlFlowMissing().isEmpty();
}

QString WorkerManager::rtlFlowMissing() const {
#ifdef EMSCRIPTEN
    return "local EDA tools (not available in the browser)";
#else
    return QString::fromStdString(acutesim::compute::orchestration::RtlFlowTools::detect().missing());
#endif
}

QString WorkerManager::submitRtlToGds(const acutesim::compute::orchestration::RtlFlowConfig& config) {
    using namespace acutesim::compute::orchestration;
    const QString jobId = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
#ifdef EMSCRIPTEN
    emit jobLogReceived(jobId, "RTL -> GDS needs local EDA tools: not available in the browser", true);
    emit jobCompleted(jobId, false, "");
#else
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    m_jobCancelFlags[jobId.toStdString()] = cancel;
    // Results go back to the GUI thread through queued calls; the thread holds the flag
    std::thread([this, jobId, config, cancel]() {
        auto post = [this](auto fn) { QMetaObject::invokeMethod(this, fn, Qt::QueuedConnection); };
        const RtlFlowResult r = runRtlToGds(
            config, RtlFlowTools::detect(),
            [&](const std::string& line, bool isError) {
                post([this, jobId, line = QString::fromStdString(line), isError]() { emit jobLogReceived(jobId, line, isError); });
            },
            [&](const std::string& stage, double progress) {
                post([this, jobId, progress, stage = QString::fromStdString(stage)]() {
                    emit jobLogReceived(jobId, "== " + stage, false);
                    emit jobProgress(jobId, (float)progress);
                });
            },
            cancel.get());
        post([this, jobId, r]() {
            m_jobCancelFlags.erase(jobId.toStdString());
            if (r.synth.success) emit synthesisResultReady(jobId, r.synth);
            if (r.pnr.success) emit pnrResultReady(jobId, r.pnr);
            if (r.drc.success) emit drcResultReady(jobId, r.drc);
            if (r.lvs.success) emit lvsResultReady(jobId, r.lvs);
            emit jobCompleted(jobId, r.success, QString::fromStdString(r.gds));
        });
    }).detach();
#endif
    return jobId;
}

void WorkerManager::cancelJob(const QString& jobId) {
    // The flow kills its tools and reports jobCompleted(false) when it sees the flag
    if (auto it = m_jobCancelFlags.find(jobId.toStdString()); it != m_jobCancelFlags.end()) {
        it->second->store(true);
        emit jobLogReceived(jobId, "Cancelling...", false);
    }
}

// ─── Tool Detection ───────────────────────────────────────────────────────────

void WorkerManager::detectLocalTools() {
#ifdef EMSCRIPTEN
    // WASM: no local tools; emit empty inventory immediately
    emit toolInventoryReady(m_inventory);
#else
    // Run detection off the main thread to avoid blocking
    std::thread([this]() {
        ToolInventory inv;

        auto probe = [](const QString& cmd, const QString& arg) -> QString {
            QProcess p;
            p.start(cmd, {arg});
            if (p.waitForFinished(3000)) {
                return QString::fromLocal8Bit(p.readAllStandardOutput())
                    .trimmed()
                    .section('\n', 0, 0); // first line
            }
            return {};
        };

        // The source-built tools live in ~/eda: RtlFlowTools knows where
        const auto tools = acutesim::compute::orchestration::RtlFlowTools::detect();
        auto at = [](const std::string& path, const char* name) { return QString::fromStdString(path.empty() ? name : path); };
        QString v;
        v = probe(at(tools.yosys, "yosys"), "-V");
        if (!v.isEmpty()) { inv.hasYosys = true; inv.yosysVersion = v; }

        v = probe(at(tools.openroad, "openroad"), "-version");
        if (!v.isEmpty()) { inv.hasOpenROAD = true; inv.openRoadVersion = v; }

        v = probe(at(tools.klayout, "klayout"), "-v");
        if (!v.isEmpty()) { inv.hasKLayout = true; inv.klayoutVersion = v; }

        v = probe("docker", "--version");
        if (!v.isEmpty()) inv.hasDocker = true;

        // PDK root discovery
        QString pdkRoot = qEnvironmentVariable("PDK_ROOT");
        if (pdkRoot.isEmpty()) {
            for (const QString& candidate : QStringList{"/opt/pdks", QDir::homePath() + "/pdks"}) {
                if (QDir(candidate).exists()) { pdkRoot = candidate; break; }
            }
        }
        inv.pdkRoot = pdkRoot;

        if (!pdkRoot.isEmpty()) {
            QDir dir(pdkRoot);
            inv.installedPDKs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        }

        QMetaObject::invokeMethod(this, [this, inv]() {
            m_inventory = inv;
            emit toolInventoryReady(m_inventory);
        }, Qt::QueuedConnection);
    }).detach();
#endif
}

} // namespace acutesim::gui::orchestration

