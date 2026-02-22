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
#ifdef EMSCRIPTEN
    // Browser: all tasks must go to a remote worker
    m_executionMode = ExecutionMode::REMOTE;
#endif
    // Start polling loop
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

QString WorkerManager::submitSynthesis(const acutesim::compute::orchestration::SynthesisConfig& config) {
    QString jobId = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);

    if (shouldRunLocally()) {
        emit jobLogReceived(jobId, "Running Yosys locally...", false);
        runLocalSynthesis(jobId, config);
        return jobId;
    }

    if (!m_client) {
        emit jobLogReceived(jobId, "No worker connected and no local tools available", true);
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
#ifdef EMSCRIPTEN
    // WASM: Run synchronously (client is stubbed anyway)
    // Note: If client implementation blocks, this blocks UI.
    // But wasm_worker_client.cpp stub is non-blocking.
    client->runSynthesis(
        grpcConfig,
        [this, jobId, cancelFlag](const std::string& line, bool isError) {
            if (cancelFlag->load()) return;
            emit jobLogReceived(jobId, QString::fromStdString(line), isError);
        },
        [this, jobId, cancelFlag](bool success, const std::string& artifactPath) {
            if (cancelFlag->load()) return;
            emit jobCompleted(jobId, success, QString::fromStdString(artifactPath));
        }
    );
#else
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
#endif

    return jobId;
}

QString WorkerManager::submitPlaceAndRoute(
    const acutesim::compute::orchestration::FloorplanConfig& fpConfig,
    const acutesim::compute::orchestration::PlacementConfig& placeConfig,
    const acutesim::compute::orchestration::CTSConfig& ctsConfig,
    const acutesim::compute::orchestration::RoutingConfig& routeConfig)
{
    QString jobId = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);

    if (shouldRunLocally()) {
        emit jobLogReceived(jobId, "Running OpenROAD locally...", false);
        runLocalPnR(jobId, fpConfig, placeConfig, ctsConfig, routeConfig);
        return jobId;
    }

    if (!m_client) {
        emit jobLogReceived(jobId, "No worker connected and no local tools available", true);
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
#ifdef EMSCRIPTEN
    client->runPlaceAndRoute(
        grpcConfig,
        [this, jobId, cancelFlag](const std::string& line, bool isError) {
            if (cancelFlag->load()) return;
            emit jobLogReceived(jobId, QString::fromStdString(line), isError);
        },
        [this, jobId, cancelFlag](float progress) {
            if (cancelFlag->load()) return;
            emit jobProgress(jobId, progress);
        },
        [this, jobId, cancelFlag](bool success, const std::string& artifactPath) {
            if (cancelFlag->load()) return;
            emit jobCompleted(jobId, success, QString::fromStdString(artifactPath));
        }
    );
#else
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
#endif

    return jobId;
}

QString WorkerManager::submitSignOff(const acutesim::compute::orchestration::DRCConfig& drcConfig) {
    QString jobId = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);

    if (shouldRunLocally()) {
        runLocalSignOff(jobId, drcConfig);
        return jobId;
    }

    if (!m_client) {
        emit jobLogReceived(jobId, "No worker connected and no local tools available", true);
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
#ifdef EMSCRIPTEN
    client->runSignOff(
        grpcConfig,
        [this, jobId, cancelFlag](const std::string& line, bool isError) {
            if (cancelFlag->load()) return;
            emit jobLogReceived(jobId, QString::fromStdString(line), isError);
        },
        [this, jobId, cancelFlag](bool success, const std::string& artifactPath) {
            if (cancelFlag->load()) return;
            emit jobCompleted(jobId, success, QString::fromStdString(artifactPath));
        }
    );
#else
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
#endif

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

// ============================================================================
// EXECUTION MODE
// ============================================================================

void WorkerManager::setExecutionMode(ExecutionMode mode) {
    m_executionMode = mode;
    emit statusChanged(m_state);
}

void WorkerManager::setMockMode(bool enabled) {
    acutesim::compute::orchestration::ToolDriver::setMockMode(enabled);
    emit statusChanged(m_state); // Trigger UI update (enable run button if formerly disabled)
}

bool WorkerManager::canRunLocally() const {
    using TD = acutesim::compute::orchestration::ToolDriver;
    return TD::isToolAvailable("yosys") || TD::isToolAvailable("openroad");
}

bool WorkerManager::shouldRunLocally() const {
    if (m_executionMode == ExecutionMode::LOCAL) return true;
    if (m_executionMode == ExecutionMode::REMOTE) return false;
    // AUTO: prefer remote if connected, fall back to local
    return !m_state.isConnected;
}

// ============================================================================
// LOCAL EXECUTION
// ============================================================================

void WorkerManager::runLocalSynthesis(
    const QString& jobId,
    const acutesim::compute::orchestration::SynthesisConfig& config) {

#ifdef EMSCRIPTEN
    emit jobLogReceived(jobId, "Local synthesis not supported in WebAssembly", true);
    emit jobCompleted(jobId, false, "");
    return;
#else
    using namespace acutesim::compute::orchestration;

    auto* cancelFlag = new std::atomic<bool>(false);
    m_jobCancelFlags[jobId.toStdString()] = cancelFlag;

    std::thread([this, jobId, config, cancelFlag]() {
        ToolDriver::LogCallback logCb = [this, jobId, cancelFlag](const QString& line, bool isError) {
            if (cancelFlag->load()) return;
            QMetaObject::invokeMethod(this, [this, jobId, line, isError]() {
                emit jobLogReceived(jobId, line, isError);
            }, Qt::QueuedConnection);
        };

        QString workDir = QDir::currentPath();
        SynthesisResult result = ToolDriver::runYosys(config, workDir, "yosys", logCb);

        if (cancelFlag->load()) return;

        QMetaObject::invokeMethod(this, [this, jobId, result]() {
            emit synthesisResultReady(jobId, result);
            emit jobCompleted(jobId, result.success, "");
        }, Qt::QueuedConnection);
        QMetaObject::invokeMethod(this, [this, jobId, result]() {
            emit synthesisResultReady(jobId, result);
            emit jobCompleted(jobId, result.success, "");
        }, Qt::QueuedConnection);
    }).detach();
#endif
}

void WorkerManager::runLocalPnR(
    const QString& jobId,
    const acutesim::compute::orchestration::FloorplanConfig& fpConfig,
    const acutesim::compute::orchestration::PlacementConfig& placeConfig,
    const acutesim::compute::orchestration::CTSConfig& ctsConfig,
    const acutesim::compute::orchestration::RoutingConfig& routeConfig) {

#ifdef EMSCRIPTEN
    emit jobLogReceived(jobId, "Local PnR not supported in WebAssembly", true);
    emit jobCompleted(jobId, false, "");
    return;
#else
    using namespace acutesim::compute::orchestration;

    auto* cancelFlag = new std::atomic<bool>(false);
    m_jobCancelFlags[jobId.toStdString()] = cancelFlag;

    std::thread([this, jobId, fpConfig, placeConfig, ctsConfig, routeConfig, cancelFlag]() {
        ToolDriver::LogCallback logCb = [this, jobId, cancelFlag](const QString& line, bool isError) {
            if (cancelFlag->load()) return;
            QMetaObject::invokeMethod(this, [this, jobId, line, isError]() {
                emit jobLogReceived(jobId, line, isError);
            }, Qt::QueuedConnection);
        };

        QString workDir = QDir::currentPath();
        PnRResult result = ToolDriver::runOpenROAD(
            fpConfig, placeConfig, ctsConfig, routeConfig, workDir, "openroad", logCb);

        if (cancelFlag->load()) return;

        QMetaObject::invokeMethod(this, [this, jobId, result]() {
            emit pnrResultReady(jobId, result);
            emit jobCompleted(jobId, result.success, "");
        }, Qt::QueuedConnection);
    }).detach();
#endif
}

void WorkerManager::runLocalSignOff(
    const QString& jobId,
    const acutesim::compute::orchestration::DRCConfig& config) {

#ifdef EMSCRIPTEN
    emit jobLogReceived(jobId, "Local SignOff not supported in WebAssembly", true);
    emit jobCompleted(jobId, false, "");
    return;
#else
    using namespace acutesim::compute::orchestration;

    auto* cancelFlag = new std::atomic<bool>(false);
    m_jobCancelFlags[jobId.toStdString()] = cancelFlag;

    std::thread([this, jobId, config, cancelFlag]() {
        ToolDriver::LogCallback logCb = [this, jobId, cancelFlag](const QString& line, bool isError) {
            if (cancelFlag->load()) return;
            QMetaObject::invokeMethod(this, [this, jobId, line, isError]() {
                emit jobLogReceived(jobId, line, isError);
            }, Qt::QueuedConnection);
        };

        QString workDir = QDir::currentPath();
        DRCResult result = ToolDriver::runKLayoutDRC(config, workDir, "klayout", logCb);

        if (cancelFlag->load()) return;

        QMetaObject::invokeMethod(this, [this, jobId, result]() {
            emit drcResultReady(jobId, result);
            emit jobCompleted(jobId, result.success, "");
        }, Qt::QueuedConnection);
    }).detach();
#endif
}

// ============================================================================
// PHYSICS COMPILER INTEGRATION
// ============================================================================

QString WorkerManager::submitPhysicsCompilation(
    const QString& modelPath,
    const QString& backend,
    bool stableTempNaming)
{
    QString jobId = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    emit jobLogReceived(jobId, "Running Physics Compiler locally...", false);
    runLocalPhysicsCompilation(jobId, modelPath, backend, stableTempNaming);
    return jobId;
}

void WorkerManager::runLocalPhysicsCompilation(
    const QString& jobId,
    const QString& modelPath,
    const QString& backend,
    bool stableTempNaming)
{
#ifdef EMSCRIPTEN
    emit jobLogReceived(jobId, "Local Physics Compilation not supported in WebAssembly", true);
    emit jobCompleted(jobId, false, "");
    return;
#else
    auto* cancelFlag = new std::atomic<bool>(false);
    m_jobCancelFlags[jobId.toStdString()] = cancelFlag;

    std::thread([this, jobId, modelPath, backend, stableTempNaming, cancelFlag]() {
        auto logCb = [this, jobId, cancelFlag](const QString& line, bool isError) {
            if (cancelFlag->load()) return;
            QMetaObject::invokeMethod(this, [this, jobId, line, isError]() {
                emit jobLogReceived(jobId, line, isError);
            }, Qt::QueuedConnection);
        };

        QString workDir = QDir::currentPath();
        QString outputDir = workDir + "/compute/generated/";

        // Step 1: Run the Physics Compiler (Python)
        QStringList compilerArgs;
        compilerArgs << "-m" << "scripts.physics_compiler"
                     << modelPath
                     << "--backend" << backend
                     << "--auto-jacobian"
                     << "--output" << outputDir;
        if (stableTempNaming) {
            compilerArgs << "--stable-temp-naming";
        }

        logCb("[PhysicsCompiler] Step 1/3: Compiling model IR...", false);

        QProcess compilerProc;
        compilerProc.setWorkingDirectory(workDir);
        compilerProc.start("python3", compilerArgs);
        compilerProc.waitForFinished(30000); // 30s timeout

        if (cancelFlag->load()) return;

        QString compilerOutput = compilerProc.readAllStandardOutput()
                               + compilerProc.readAllStandardError();
        logCb(compilerOutput.trimmed(), compilerProc.exitCode() != 0);

        if (compilerProc.exitCode() != 0) {
            logCb("[PhysicsCompiler] FAILED: Compiler returned non-zero exit code.", true);
            QMetaObject::invokeMethod(this, [this, jobId]() {
                emit physicsCompilationReady(jobId, false, "", "");
                emit jobCompleted(jobId, false, "");
            }, Qt::QueuedConnection);
            return;
        }

        // Determine the generated header path
        QFileInfo modelInfo(modelPath);
        QString baseName = modelInfo.baseName().toLower();
        QString headerPath = outputDir + "gen_" + baseName + ".h";

        logCb("[PhysicsCompiler] Step 2/3: Generating shared library shim...", false);

        // Step 2: Generate a shim .cpp that includes the header
        QString shimPath = outputDir + "shim_" + baseName + ".cpp";
        QFile shimFile(shimPath);
        if (shimFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream ts(&shimFile);
            ts << "// AUTO-GENERATED shim for dynamic loading\n"
               << "#include \"" << QFileInfo(headerPath).fileName() << "\"\n";
            shimFile.close();
        }

        // Step 3: Compile the shim into a shared library
        logCb("[PhysicsCompiler] Step 3/3: Compiling shared library...", false);

#ifdef __APPLE__
        QString libExt = ".dylib";
        QStringList clangArgs = {
            "-shared", "-fPIC",
            "-std=c++17",
            "-fno-fast-math", "-ffp-contract=off",
            "-I", workDir + "/compute",
            "-I", outputDir,
            shimPath,
            "-o", outputDir + "lib" + baseName + libExt,
        };
#else
        QString libExt = ".so";
        QStringList clangArgs = {
            "-shared", "-fPIC",
            "-std=c++17",
            "-fno-fast-math", "-ffp-contract=off",
            "-I", workDir + "/compute",
            "-I", outputDir,
            shimPath,
            "-o", outputDir + "lib" + baseName + libExt,
        };
#endif

        QProcess clangProc;
        clangProc.setWorkingDirectory(workDir);
        clangProc.start("clang++", clangArgs);
        clangProc.waitForFinished(60000); // 60s timeout

        if (cancelFlag->load()) return;

        QString clangOutput = clangProc.readAllStandardOutput()
                            + clangProc.readAllStandardError();
        if (!clangOutput.trimmed().isEmpty()) {
            logCb(clangOutput.trimmed(), clangProc.exitCode() != 0);
        }

        QString sharedLibPath = outputDir + "lib" + baseName + libExt;
        bool success = clangProc.exitCode() == 0;

        if (success) {
            logCb("[PhysicsCompiler] SUCCESS: " + sharedLibPath, false);
        } else {
            logCb("[PhysicsCompiler] FAILED: clang++ compilation failed.", true);
            sharedLibPath.clear();
        }

        QMetaObject::invokeMethod(this, [this, jobId, success, headerPath, sharedLibPath]() {
            emit physicsCompilationReady(jobId, success, headerPath, sharedLibPath);
            emit jobCompleted(jobId, success, sharedLibPath);
        }, Qt::QueuedConnection);
    }).detach();
#endif
}

// ─── Tool Detection ───────────────────────────────────────────────────────────

void WorkerManager::detectLocalTools() {
#ifdef EMSCRIPTEN
    // WASM: no local tools; emit empty inventory immediately
    emit toolInventoryReady(m_inventory);
    m_executionMode = ExecutionMode::REMOTE;
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

        QString v;
        v = probe("yosys", "--version");
        if (!v.isEmpty()) { inv.hasYosys = true; inv.yosysVersion = v; }

        v = probe("openroad", "-version");
        if (!v.isEmpty()) { inv.hasOpenROAD = true; inv.openRoadVersion = v; }

        v = probe("klayout", "--version");
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

