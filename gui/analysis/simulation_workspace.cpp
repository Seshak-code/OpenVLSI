#include "simulation_workspace.h"
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QLineEdit>
#include <QCheckBox>
#include <QUuid>
#include <QDateTime>

SimulationWorkspace::SimulationWorkspace(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    
    // Initialize default profiles
    profiles_["quick_dc"] = Simulation::SimulationProfile::quickDC();
    profiles_["transient_default"] = Simulation::SimulationProfile::transient();
    profiles_["ac_sweep_default"] = Simulation::SimulationProfile::acSweep();
    
    currentProfileId_ = "quick_dc";
    
    // Simulation timer
    simTimer_ = new QTimer(this);
    connect(simTimer_, &QTimer::timeout, this, &SimulationWorkspace::stepSimulation);
}

void SimulationWorkspace::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    
    // Toolbar at top
    setupToolbar();
    mainLayout->addWidget(toolbar_);
    
    // Main content splitter
    mainSplitter_ = new QSplitter(Qt::Horizontal, this);
    
    // Left: Explorer
    explorer_ = new SimulationExplorer(this);
    explorer_->setMinimumWidth(180);
    explorer_->setMaximumWidth(300);
    mainSplitter_->addWidget(explorer_);
    
    connect(explorer_, &SimulationExplorer::profileSelected,
            this, &SimulationWorkspace::onProfileSelected);
    connect(explorer_, &SimulationExplorer::runRequested,
            this, &SimulationWorkspace::onRunRequested);
    
    // Right side: vertical splitter (editor + output)
    rightSplitter_ = new QSplitter(Qt::Vertical, this);
    
    // Center: Profile Editor
    setupProfileEditor();
    rightSplitter_->addWidget(profileEditor_);
    
    // Bottom: Output Tabs
    setupOutputTabs();
    rightSplitter_->addWidget(outputTabs_);
    
    rightSplitter_->setStretchFactor(0, 2);
    rightSplitter_->setStretchFactor(1, 1);
    
    mainSplitter_->addWidget(rightSplitter_);
    mainSplitter_->setStretchFactor(0, 0);
    mainSplitter_->setStretchFactor(1, 1);
    
    mainLayout->addWidget(mainSplitter_);
}

void SimulationWorkspace::setupToolbar() {
    toolbar_ = new QWidget(this);
    toolbar_->setFixedHeight(40);
    toolbar_->setStyleSheet("background-color: #333; border-bottom: 1px solid #444;");
    
    QHBoxLayout* layout = new QHBoxLayout(toolbar_);
    layout->setContentsMargins(8, 4, 8, 4);
    
    // Profile selector
    QLabel* profileLabel = new QLabel("Profile:", toolbar_);
    profileLabel->setStyleSheet("color: #888;");
    layout->addWidget(profileLabel);
    
    profileCombo_ = new QComboBox(toolbar_);
    profileCombo_->addItem("Quick DC", "quick_dc");
    profileCombo_->addItem("Transient", "transient_default");
    profileCombo_->addItem("AC Sweep", "ac_sweep_default");
    profileCombo_->setMinimumWidth(120);
    layout->addWidget(profileCombo_);
    
    connect(profileCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        QString id = profileCombo_->itemData(idx).toString();
        selectProfile(id);
    });
    
    layout->addSpacing(20);
    
    // Run button
    runBtn_ = new QPushButton("Run", toolbar_);
    runBtn_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    runBtn_->setStyleSheet("QPushButton { background-color: #0e639c; color: white; padding: 4px 12px; }");
    connect(runBtn_, &QPushButton::clicked, this, &SimulationWorkspace::runCurrent);
    layout->addWidget(runBtn_);
    
    // Stop button
    stopBtn_ = new QPushButton("Stop", toolbar_);
    stopBtn_->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    stopBtn_->setEnabled(false);
    connect(stopBtn_, &QPushButton::clicked, this, &SimulationWorkspace::stopSimulation);
    layout->addWidget(stopBtn_);
    
    layout->addStretch();
    
    // Status indicator
    QLabel* statusLabel = new QLabel("Ready", toolbar_);
    statusLabel->setStyleSheet("color: #4ec9b0;");
    statusLabel->setObjectName("statusLabel");
    layout->addWidget(statusLabel);
}

void SimulationWorkspace::setupProfileEditor() {
    profileEditor_ = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(profileEditor_);
    layout->setContentsMargins(12, 12, 12, 12);
    
    // Profile name header
    profileNameLabel_ = new QLabel("Quick DC", profileEditor_);
    profileNameLabel_->setStyleSheet("font-size: 16px; font-weight: bold; color: #ccc;");
    layout->addWidget(profileNameLabel_);
    
    // Analysis type selector
    QHBoxLayout* analysisLayout = new QHBoxLayout();
    analysisLayout->addWidget(new QLabel("Analysis:", profileEditor_));
    
    analysisCombo_ = new QComboBox(profileEditor_);
    analysisCombo_->addItem("DC", static_cast<int>(Simulation::AnalysisType::DC));
    analysisCombo_->addItem("AC", static_cast<int>(Simulation::AnalysisType::AC));
    analysisCombo_->addItem("Transient", static_cast<int>(Simulation::AnalysisType::TRANSIENT));
    analysisCombo_->addItem("Noise", static_cast<int>(Simulation::AnalysisType::NOISE));
    
    connect(analysisCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        auto type = static_cast<Simulation::AnalysisType>(analysisCombo_->itemData(idx).toInt());
        updateParametersForAnalysis(type);
    });
    
    analysisLayout->addWidget(analysisCombo_);
    analysisLayout->addStretch();
    layout->addLayout(analysisLayout);
    
    // Parameters area (scrollable)
    QScrollArea* scroll = new QScrollArea(profileEditor_);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    
    parametersWidget_ = new QWidget(scroll);
    parametersWidget_->setLayout(new QFormLayout());
    scroll->setWidget(parametersWidget_);
    
    layout->addWidget(scroll);
    
    updateProfileEditor();
}

void SimulationWorkspace::setupOutputTabs() {
    outputTabs_ = new QTabWidget(this);
    outputTabs_->setStyleSheet(R"(
        QTabWidget::pane { border: none; background: #1e1e1e; }
        QTabBar::tab { background: #2d2d2d; color: #888; padding: 6px 12px; }
        QTabBar::tab:selected { background: #1e1e1e; color: #ccc; }
    )");
    
    // Output log
    outputLog_ = new QPlainTextEdit(this);
    outputLog_->setReadOnly(true);
    outputLog_->setStyleSheet("background: #1e1e1e; color: #ccc; font-family: monospace;");
    outputTabs_->addTab(outputLog_, "Output");
    
    // Streaming log
    streamingLog_ = new QPlainTextEdit(this);
    streamingLog_->setReadOnly(true);
    streamingLog_->setStyleSheet("background: #1e1e1e; color: #4ec9b0; font-family: monospace;");
    outputTabs_->addTab(streamingLog_, "Streaming");
    
    // Results (placeholder)
    resultsPanel_ = new QWidget(this);
    resultsPanel_->setLayout(new QVBoxLayout());
    static_cast<QVBoxLayout*>(resultsPanel_->layout())->addWidget(
        new QLabel("Results will appear here after simulation"));
    outputTabs_->addTab(resultsPanel_, "Results");
    
    // Waveforms (placeholder)
    waveformPanel_ = new QWidget(this);
    waveformPanel_->setLayout(new QVBoxLayout());
    static_cast<QVBoxLayout*>(waveformPanel_->layout())->addWidget(
        new QLabel("Waveforms will appear here"));
    outputTabs_->addTab(waveformPanel_, "Waveforms");
}

void SimulationWorkspace::setSchematicEditor(SchematicEditor* editor) {
    schematicEditor_ = editor;
}

void SimulationWorkspace::selectProfile(const QString& profileId) {
    if (!profiles_.contains(profileId)) return;
    
    currentProfileId_ = profileId;
    updateProfileEditor();
    
    // Update combo
    for (int i = 0; i < profileCombo_->count(); ++i) {
        if (profileCombo_->itemData(i).toString() == profileId) {
            profileCombo_->setCurrentIndex(i);
            break;
        }
    }
    
    emit explorer_->profileSelected(profileId);
}

Simulation::SimulationProfile* SimulationWorkspace::currentProfile() {
    if (profiles_.contains(currentProfileId_)) {
        return &profiles_[currentProfileId_];
    }
    return nullptr;
}

void SimulationWorkspace::updateProfileEditor() {
    auto* profile = currentProfile();
    if (!profile) return;
    
    profileNameLabel_->setText(profile->name);
    
    // Set analysis combo
    for (int i = 0; i < analysisCombo_->count(); ++i) {
        if (analysisCombo_->itemData(i).toInt() == static_cast<int>(profile->analysis)) {
            analysisCombo_->setCurrentIndex(i);
            break;
        }
    }
    
    updateParametersForAnalysis(profile->analysis);
}

void SimulationWorkspace::updateParametersForAnalysis(Simulation::AnalysisType type) {
    // Clear existing parameters
    QFormLayout* form = static_cast<QFormLayout*>(parametersWidget_->layout());
    while (form->count() > 0) {
        QLayoutItem* item = form->takeAt(0);
        if (item->widget()) delete item->widget();
        delete item;
    }
    
    auto* profile = currentProfile();
    
    // Add analysis-specific parameters
    switch (type) {
        case Simulation::AnalysisType::DC:
            form->addRow("Accuracy:", createComboParam({"liberal", "moderate", "conservative"}, 
                         profile ? profile->parameters.value("accuracy", "moderate").toString() : "moderate"));
            form->addRow("Max Iterations:", createLineParam(
                         profile ? profile->parameters.value("maxIter", "50").toString() : "50"));
            break;
            
        case Simulation::AnalysisType::TRANSIENT:
            form->addRow("Stop Time:", createLineParam(
                         profile ? profile->parameters.value("stopTime", "1ms").toString() : "1ms"));
            form->addRow("Step Hint:", createLineParam(
                         profile ? profile->parameters.value("stepHint", "100ns").toString() : "100ns"));
            form->addRow("Accuracy:", createComboParam({"liberal", "moderate", "conservative"}, "moderate"));
            break;
            
        case Simulation::AnalysisType::AC:
            form->addRow("Start Freq:", createLineParam(
                         profile ? profile->parameters.value("fStart", "1Hz").toString() : "1Hz"));
            form->addRow("Stop Freq:", createLineParam(
                         profile ? profile->parameters.value("fStop", "1GHz").toString() : "1GHz"));
            form->addRow("Points:", createLineParam(
                         profile ? profile->parameters.value("points", "200").toString() : "200"));
            form->addRow("Sweep Type:", createComboParam({"log", "linear"}, "log"));
            break;
            
        case Simulation::AnalysisType::NOISE:
            form->addRow("Start Freq:", createLineParam("1Hz"));
            form->addRow("Stop Freq:", createLineParam("1MHz"));
            form->addRow("Input Source:", createLineParam("Vin"));
            form->addRow("Output Node:", createLineParam("Vout"));
            break;
            
        default:
            break;
    }
}

// Helper: create line edit parameter
QWidget* SimulationWorkspace::createLineParam(const QString& defaultVal) {
    QLineEdit* edit = new QLineEdit(defaultVal, parametersWidget_);
    edit->setStyleSheet("background: #3c3c3c; color: #ccc; padding: 4px;");
    return edit;
}

// Helper: create combo parameter
QWidget* SimulationWorkspace::createComboParam(const QStringList& options, const QString& defaultVal) {
    QComboBox* combo = new QComboBox(parametersWidget_);
    combo->addItems(options);
    combo->setCurrentText(defaultVal);
    return combo;
}

void SimulationWorkspace::onTopologyChanged(size_t newHash) {
    if (newHash != currentTopologyHash_) {
        currentTopologyHash_ = newHash;
        explorer_->markAllStale();
        appendLog(QString("[INFO] Topology changed (hash: 0x%1). Profiles marked stale.")
                  .arg(newHash, 8, 16, QChar('0')));
    }
}

bool SimulationWorkspace::canRun() const {
    if (!schematicEditor_) return false;
    
    auto* scene = schematicEditor_->getScene();
    if (!scene) return false;
    
    // Check ERC
    if (!scene->canSimulate()) {
        return false;
    }
    
    return true;
}

void SimulationWorkspace::runCurrent() {
    onRunRequested(currentProfileId_);
}

void SimulationWorkspace::runQuickDC() {
    selectProfile("quick_dc");
    runCurrent();
}

void SimulationWorkspace::onProfileSelected(const QString& profileId) {
    selectProfile(profileId);
}

void SimulationWorkspace::onRunRequested(const QString& profileId) {
    if (!canRun()) {
        appendLog("[ERROR] Cannot run: ERC violations present or no schematic.");
        return;
    }
    
    // Get netlist
    netlist_ = schematicEditor_->toNetlist();
    
    // Create run object
    currentRun_ = Simulation::SimulationRun();
    currentRun_.runId = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    currentRun_.profileId = profileId;
    currentRun_.topologyHash = currentTopologyHash_;
    currentRun_.startTime = QDateTime::currentDateTime();
    currentRun_.status = Simulation::SimulationRun::Status::RUNNING;
    
    // Update UI
    isRunning_ = true;
    runBtn_->setEnabled(false);
    stopBtn_->setEnabled(true);
    findChild<QLabel*>("statusLabel")->setText("Running...");
    findChild<QLabel*>("statusLabel")->setStyleSheet("color: #dcdcaa;");
    
    // Clear streaming log
    streamingLog_->clear();
    
    appendLog(QString("[%1] Starting %2...")
              .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
              .arg(profiles_[profileId].name));
    
    emit simulationStarted(profileId);
    
    // Start DC solve
    currentTime_ = 0.0;
    
    auto* profile = currentProfile();
    if (profile && profile->analysis == Simulation::AnalysisType::DC) {
        // Quick DC - single step
        appendStreamLog("DC Analysis starting...");
        SolverStep result = sim_.solveDC(netlist_);
        
        currentRun_.status = result.stats.converged ? 
            Simulation::SimulationRun::Status::CONVERGED : 
            Simulation::SimulationRun::Status::FAILED;
        currentRun_.iterations = result.stats.iterations;
        currentRun_.endTime = QDateTime::currentDateTime();
        currentRun_.solveTime_ms = currentRun_.startTime.msecsTo(currentRun_.endTime);
        
        if (result.stats.converged) {
            appendStreamLog(QString("Converged in %1 iterations")
                           .arg(result.stats.iterations));
            for (size_t i = 0; i < result.nodeVoltages.size(); ++i) {
                appendStreamLog(QString("  V(%1) = %2 V")
                               .arg(i+1).arg(result.nodeVoltages[i], 0, 'g', 4));
            }
            explorer_->updateProfileStatus(profileId, Simulation::SimulationProfile::Status::PASSED);
        } else {
            appendStreamLog("FAILED to converge");
            currentRun_.failureReason = result.stats.error_detail.empty() ? 
                "Non-convergence" : QString::fromStdString(result.stats.error_detail);
            explorer_->updateProfileStatus(profileId, Simulation::SimulationProfile::Status::FAILED);
        }
        
        finishSimulation();
    } else {
        // Transient - use timer
        simTimer_->start(10); // 10ms between steps (for UI responsiveness)
    }
}

void SimulationWorkspace::stepSimulation() {
    double stopTime = 0.001; // 1ms default
    
    auto* profile = currentProfile();
    if (profile) {
        QString stopStr = profile->parameters.value("stopTime", "1ms").toString();
        // Parse stop time (simplified)
        if (stopStr.endsWith("ms")) stopTime = stopStr.left(stopStr.length()-2).toDouble() * 1e-3;
        else if (stopStr.endsWith("us")) stopTime = stopStr.left(stopStr.length()-2).toDouble() * 1e-6;
        else if (stopStr.endsWith("s")) stopTime = stopStr.left(stopStr.length()-1).toDouble();
    }
    
    // Run several steps per timer tick
    for (int i = 0; i < 10 && currentTime_ < stopTime; ++i) {
        SolverStep result = sim_.stepTransient(netlist_, timeStep_, currentTime_);
        currentTime_ += timeStep_;
        currentRun_.iterations++;
        
        if (!result.stats.converged) {
            currentRun_.status = Simulation::SimulationRun::Status::FAILED;
            currentRun_.failureReason = result.stats.error_detail.empty() ? 
                "Transient step failed" : QString::fromStdString(result.stats.error_detail);
            simTimer_->stop();
            finishSimulation();
            return;
        }
    }
    
    // Update streaming log periodically
    if (static_cast<int>(currentTime_ / timeStep_) % 100 == 0) {
        appendStreamLog(QString("t = %1 us").arg(currentTime_ * 1e6, 0, 'f', 2));
    }
    
    // Check completion
    if (currentTime_ >= stopTime) {
        currentRun_.status = Simulation::SimulationRun::Status::CONVERGED;
        simTimer_->stop();
        finishSimulation();
    }
}

void SimulationWorkspace::finishSimulation() {
    isRunning_ = false;
    runBtn_->setEnabled(true);
    stopBtn_->setEnabled(false);
    
    currentRun_.endTime = QDateTime::currentDateTime();
    currentRun_.solveTime_ms = currentRun_.startTime.msecsTo(currentRun_.endTime);
    
    // Add to history
    explorer_->addRun(currentRun_);
    runHistory_.prepend(currentRun_);
    
    // Update status
    bool passed = currentRun_.status == Simulation::SimulationRun::Status::CONVERGED;
    findChild<QLabel*>("statusLabel")->setText(passed ? "Passed" : "Failed");
    findChild<QLabel*>("statusLabel")->setStyleSheet(passed ? "color: #4ec9b0;" : "color: #f44747;");
    
    appendLog(QString("[%1] %2: %3 (%4 ms)")
              .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
              .arg(profiles_[currentProfileId_].name)
              .arg(passed ? "PASSED" : "FAILED")
              .arg(currentRun_.solveTime_ms, 0, 'f', 1));
    
    emit simulationCompleted(currentRun_);
}

void SimulationWorkspace::stopSimulation() {
    if (isRunning_) {
        simTimer_->stop();
        currentRun_.status = Simulation::SimulationRun::Status::CANCELLED;
        finishSimulation();
        appendLog("[INFO] Simulation cancelled by user.");
    }
}

void SimulationWorkspace::appendLog(const QString& msg) {
    outputLog_->appendPlainText(msg);
}

void SimulationWorkspace::appendStreamLog(const QString& msg) {
    streamingLog_->appendPlainText(msg);
}
