#include "simulation_workspace.h"
#include "monte_carlo_setup_dialog.h"
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QLineEdit>
#include <QCheckBox>
#include <QUuid>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QMessageBox>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QDialog>
#include <QDockWidget>
#include <QMainWindow>
#include "corner_setup_widget.h"
#include "../../physics/quick_analysis.h"
#include "../../physics/statistical_engine.h"
#include "statistical_dashboard.h"
#include <random>
#include <numeric>
#include <map>
#include <QDialogButtonBox>
#include <QSpinBox>

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
    
    // Left: Schematic file selector
    QWidget* leftPanel = new QWidget(this);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(8, 8, 8, 8);
    
    QLabel* fileLabel = new QLabel("Schematic:", leftPanel);
    fileLabel->setStyleSheet("color: #ccc; font-weight: bold;");
    leftLayout->addWidget(fileLabel);
    
    schematicCombo_ = new QComboBox(leftPanel);
    schematicCombo_->setStyleSheet("background: #333; color: #ccc; padding: 4px;");
    leftLayout->addWidget(schematicCombo_);
    
    leftLayout->addStretch();
    leftPanel->setMinimumWidth(180);
    leftPanel->setMaximumWidth(250);
    leftPanel->setStyleSheet("background: #252526;");
    mainSplitter_->addWidget(leftPanel);
    
    // Right: Profile Editor + Help Panel (no output tabs)
    QWidget* rightPanel = new QWidget(this);
    QVBoxLayout* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    
    // Profile Editor
    setupProfileEditor();
    rightLayout->addWidget(profileEditor_, 1);
    
    // Help/Explanation panel
    helpLabel_ = new QLabel(rightPanel);
    helpLabel_->setWordWrap(true);
    helpLabel_->setStyleSheet("background: #1e1e1e; color: #888; padding: 12px; font-size: 11px;");
    helpLabel_->setText(getAnalysisHelp(Simulation::AnalysisType::DC));
    rightLayout->addWidget(helpLabel_);
    
    mainSplitter_->addWidget(rightPanel);
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
    
    layout->addSpacing(10);
    
    // Run mode selector
    QLabel* modeLabel = new QLabel("Mode:", toolbar_);
    modeLabel->setStyleSheet("color: #888;");
    layout->addWidget(modeLabel);
    
    runModeCombo_ = new QComboBox(toolbar_);
    runModeCombo_->addItem("Full", static_cast<int>(acutesim::physics::RunMode::FULL));
    runModeCombo_->addItem("Quick", static_cast<int>(acutesim::physics::RunMode::QUICK));
    runModeCombo_->addItem("Resume", static_cast<int>(acutesim::physics::RunMode::RESUME));
    runModeCombo_->addItem("Incremental", static_cast<int>(acutesim::physics::RunMode::INCREMENTAL));
    runModeCombo_->setMinimumWidth(90);
    layout->addWidget(runModeCombo_);
    
    layout->addSpacing(10);
    
    // Corner selector
    QLabel* cornerLabel = new QLabel("Corner:", toolbar_);
    cornerLabel->setStyleSheet("color: #888;");
    layout->addWidget(cornerLabel);
    
    cornerCombo_ = new QComboBox(toolbar_);
    int i = 0;
    for (const auto& c : acutesim::physics::CornerConfig::standard5()) {
        cornerCombo_->addItem(c.name, QVariant::fromValue(c));
        cornerCombo_->setItemData(i, QString("%1C, Vdd x%2").arg(c.temperatureC).arg(c.voltageScale), Qt::ToolTipRole);
        i++;
    }
    layout->addWidget(cornerCombo_);

    // Configure Button (Gear Icon)
    QPushButton* configCornersBtn = new QPushButton(toolbar_);
    configCornersBtn->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    configCornersBtn->setToolTip("Configure PVT / Monte Carlo...");
    configCornersBtn->setFixedWidth(30);
    connect(configCornersBtn, &QPushButton::clicked, this, &SimulationWorkspace::configureCorners);
    layout->addWidget(configCornersBtn);
    
    layout->addSpacing(10);
    
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
    
    // Trust Badge (Phase 1.7.9)
    trustBadge_ = new TrustBadgeWidget(toolbar_);
    trustBadge_->setMinimumWidth(200);
    layout->addWidget(trustBadge_);
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
    QStandardItemModel* model = new QStandardItemModel(analysisCombo_);
    analysisCombo_->setModel(model);
    
    auto addItem = [&](const QString& text, Simulation::AnalysisType type) {
        QStandardItem* item = new QStandardItem(text);
        item->setData(static_cast<int>(type), Qt::UserRole);
        model->appendRow(item);
    };
    
    addItem("DC", Simulation::AnalysisType::DC);
    addItem("AC", Simulation::AnalysisType::AC);
    addItem("Transient", Simulation::AnalysisType::TRANSIENT);
    addItem("Noise", Simulation::AnalysisType::NOISE);
    
    // Phase 1.1: Statistical Modes
    addItem("N-runs (Monte Carlo)", Simulation::AnalysisType::MONTE_CARLO);
    addItem("Sensitivity", Simulation::AnalysisType::SENSITIVITY);
    
    connect(analysisCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        if (idx < 0) return;
        auto type = static_cast<Simulation::AnalysisType>(analysisCombo_->itemData(idx).toInt());
        
        // Enforce mode restrictions (redundant safety for UI)
        bool allowed = true;
        if (currentMode_ == AppMode::BLOCK_SUBSYSTEM) {
            if (type == Simulation::AnalysisType::AC || type == Simulation::AnalysisType::NOISE) allowed = false;
        } else if (currentMode_ == AppMode::SYSTEM_INTEGRATION) {
            if (type != Simulation::AnalysisType::TRANSIENT) allowed = false;
        }

        if (!allowed) {
            // Revert to a safe default (Transient is always safe in restricted modes)
            int safeIdx = analysisCombo_->findData(static_cast<int>(Simulation::AnalysisType::TRANSIENT));
            if (safeIdx >= 0 && safeIdx != idx) {
                analysisCombo_->setCurrentIndex(safeIdx);
            }
            return;
        }

        updateParametersForAnalysis(type);
        
        // Update help text for the selected analysis
        if (helpLabel_) {
            helpLabel_->setText(getAnalysisHelp(type));
        }
    });
    
    analysisLayout->addWidget(analysisCombo_);
    
    // Advanced toggle
    advancedParamsCheck_ = new QCheckBox("Advanced", profileEditor_);
    advancedParamsCheck_->setStyleSheet("color: #aaa; margin-left: 10px;");
    connect(advancedParamsCheck_, &QCheckBox::toggled, this, [this](bool){
        updateProfileEditor(); // Re-render parameters
    });
    analysisLayout->addWidget(advancedParamsCheck_);
    
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

QString SimulationWorkspace::getAnalysisHelp(Simulation::AnalysisType type) const {
    switch (type) {
        case Simulation::AnalysisType::DC:
            return "DC Analysis finds the steady-state operating point of the circuit.\n\n"
                   "• Computes DC voltages and currents at all nodes\n"
                   "• Uses Newton-Raphson iteration for nonlinear devices\n"
                   "• Required before AC or Noise analysis\n\n"
                   "Use for: Bias point verification, power consumption calculations.";
        case Simulation::AnalysisType::AC:
            return "AC Analysis computes small-signal frequency response.\n\n"
                   "• Linearizes circuit around DC operating point\n"
                   "• Sweeps frequency to generate Bode plots\n"
                   "• Shows gain, phase, and impedance vs frequency\n\n"
                   "Use for: Amplifier gain, filter cutoff, stability analysis.";
        case Simulation::AnalysisType::TRANSIENT:
            return "Transient Analysis simulates time-domain behavior.\n\n"
                   "• Computes voltages/currents vs time\n"
                   "• Uses adaptive timestep integration\n"
                   "• Handles switching and nonlinear dynamics\n\n"
                   "Use for: Digital logic, switching waveforms, power-on behavior.";
        case Simulation::AnalysisType::NOISE:
            return "Noise Analysis computes equivalent input/output noise.\n\n"
                   "• Models thermal, shot, and flicker noise sources\n"
                   "• Requires specified input/output ports\n"
                   "• Reports noise spectral density and integrated noise\n\n"
                   "Use for: Low-noise amplifier design, SNR optimization.";
        case Simulation::AnalysisType::MONTE_CARLO:
            return "Monte Carlo Analysis performs statistical yield verification.\n\n"
                   "• Runs batch simulations with random component variations\n"
                   "• Computes Mean (μ), Std Dev (σ), and Yield (%)\n"
                   "• Supports Gaussian and Uniform distributions\n\n"
                   "Use for: Design for Manufacturing (DFM), yield estimation.";
        case Simulation::AnalysisType::SENSITIVITY:
            return "Sensitivity Analysis identifies critical components.\n\n"
                   "• Perturbs each component to measure output impact\n"
                   "• Ranks components by 'Normalized Sensitivity'\n"
                   "• Helps identify which parts need tighter tolerances\n\n"
                   "Use for: Cost optimization, circuit tuning.";
        default:
            return "Select an analysis type to see its description.";
    }
}

void SimulationWorkspace::setSchematicEditor(SchematicEditor* editor) {
    // Disconnect previous editor if any
    if (schematicEditor_) {
        disconnect(schematicEditor_, nullptr, this, nullptr);
        
        // Save current profiles back to old editor
        schematicEditor_->setProfiles(profiles_);
        schematicEditor_->setCurrentProfileId(currentProfileId_);
    }
    
    schematicEditor_ = editor;
    if (!editor) return;
    
    // Load profiles from new editor
    profiles_ = editor->getProfiles();
    currentProfileId_ = editor->getCurrentProfileId();
    
    // If editor has no profiles, initialize defaults
    if (profiles_.isEmpty()) {
        profiles_["quick_dc"] = Simulation::SimulationProfile::quickDC();
        profiles_["transient_default"] = Simulation::SimulationProfile::transient();
        profiles_["ac_sweep_default"] = Simulation::SimulationProfile::acSweep();
        currentProfileId_ = "quick_dc";
    }
    
    // Connect topology changes
    connect(editor, &SchematicEditor::topologyChanged, this, &SimulationWorkspace::onTopologyChanged);
    
    // Connect mode changes
    connect(editor, &SchematicEditor::modeChanged, this, &SimulationWorkspace::onModeChanged);
    
    // Connect ERC violations - now handled in MainWindow bottom dock
    // connect(editor, &SchematicEditor::violationsChanged, problemsPanel_, &ProblemsPanel::setViolations);
    
    // Phase 7: Populate schematic combo with current editor
    if (schematicCombo_) {
        schematicCombo_->clear();
        schematicCombo_->addItem("Current Schematic");
    }
    
    // Initial sync
    onModeChanged(editor->getMode());
    updateProfileEditor();
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
    
    // Notify profile change (explorer removed - UI simplified)
    // emit explorer_->profileSelected(profileId);
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
    bool showAdvanced = advancedParamsCheck_ && advancedParamsCheck_->isChecked();
    
    // Add analysis-specific parameters
    switch (type) {
        case Simulation::AnalysisType::DC:
            // DC is usually parameter-free unless iterating options
            if (showAdvanced) {
                form->addRow("Accuracy:", createComboParam({"liberal", "moderate", "conservative"}, 
                             profile ? profile->parameters.value("accuracy", "moderate").toString() : "moderate"));
                form->addRow("Max Iterations:", createLineParam(
                             profile ? profile->parameters.value("maxIter", "50").toString() : "50"));
            } else {
                 form->addRow(new QLabel("Standard DC Analysis (Default)", parametersWidget_));
            }
            break;
            
        case Simulation::AnalysisType::TRANSIENT:
            form->addRow("Stop Time:", createLineParam(
                         profile ? profile->parameters.value("stopTime", "1ms").toString() : "1ms"));
            if (showAdvanced) {
                form->addRow("Step Hint:", createLineParam(
                             profile ? profile->parameters.value("stepHint", "100ns").toString() : "100ns"));
                form->addRow("Accuracy:", createComboParam({"liberal", "moderate", "conservative"}, "moderate"));
            }
            break;
            
        case Simulation::AnalysisType::AC:
            form->addRow("Start Freq:", createLineParam(
                         profile ? profile->parameters.value("fStart", "1Hz").toString() : "1Hz"));
            form->addRow("Stop Freq:", createLineParam(
                         profile ? profile->parameters.value("fStop", "1GHz").toString() : "1GHz"));
            if (showAdvanced) {
                form->addRow("Points:", createLineParam(
                             profile ? profile->parameters.value("points", "200").toString() : "200"));
                form->addRow("Sweep Type:", createComboParam({"log", "linear"}, "log"));
            }
            break;
            
        case Simulation::AnalysisType::NOISE:
            form->addRow("Input Source:", createLineParam("Vin"));
            form->addRow("Output Node:", createLineParam("Vout"));
            if (showAdvanced) {
                form->addRow("Start Freq:", createLineParam("1Hz"));
                form->addRow("Stop Freq:", createLineParam("1MHz"));
            }
            break;

        case Simulation::AnalysisType::MONTE_CARLO:
            // Handled via modal dialog, but we place a "Configure" button placeholder
            if (1) { // Scope
                QPushButton* btn = new QPushButton("Configure Monte Carlo...", parametersWidget_);
                connect(btn, &QPushButton::clicked, this, &SimulationWorkspace::runMonteCarlo);
                form->addRow(btn);
            }
            break;

        case Simulation::AnalysisType::SENSITIVITY:
            if (1) {
                QPushButton* btn = new QPushButton("Run Sensitivity Analysis", parametersWidget_);
                connect(btn, &QPushButton::clicked, this, &SimulationWorkspace::runSensitivity);
                form->addRow(btn);
            }
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
        // explorer_->markAllStale(); // Explorer removed
        QuickAnalysis::invalidate(); // Invalidate hover preview cache
        appendLog(QString("[INFO] Topology changed (hash: 0x%1). Profiles marked stale.")
                  .arg(newHash, 8, 16, QChar('0')));
    }
}

void SimulationWorkspace::onModeChanged(AppMode mode) {
    currentMode_ = mode;
    updateAnalysisVisibility();
    
    // Force parameter update if analysis type changed or needs filtering
    if (currentProfile()) {
        updateProfileEditor(); 
    }
}

void SimulationWorkspace::updateAnalysisVisibility() {
    if (!analysisCombo_) return;
    
    QStandardItemModel* model = qobject_cast<QStandardItemModel*>(analysisCombo_->model());
    if (!model) {
        appendLog("[ERROR] analysisCombo model is not QStandardItemModel");
        return;
    }
    
    appendLog(QString("[DEBUG] Updating visibility for mode: %1").arg(static_cast<int>(currentMode_)));
    
    // Rules:
    // BENCH: All allowed
    // BLOCK: DC, TRANSIENT only (No AC/Noise/Sensitivity)
    // SYSTEM: TRANSIENT only
    
    for (int i = 0; i < analysisCombo_->count(); ++i) {
        Simulation::AnalysisType type = static_cast<Simulation::AnalysisType>(analysisCombo_->itemData(i).toInt());
        bool allowed = true;
        
        if (currentMode_ == AppMode::BLOCK_SUBSYSTEM) {
            if (type == Simulation::AnalysisType::AC || type == Simulation::AnalysisType::NOISE) {
                allowed = false;
            }
        } else if (currentMode_ == AppMode::SYSTEM_INTEGRATION) {
            if (type != Simulation::AnalysisType::TRANSIENT) {
                allowed = false;
            }
        }
        
        QStandardItem* item = model->item(i);
        if (item) {
            item->setEnabled(allowed); 
            
            // Visual indication (grey out text)
            if (!allowed) item->setForeground(QBrush(Qt::gray));
            else item->setForeground(QBrush(Qt::white));
            
            if (!allowed && i == analysisCombo_->currentIndex()) {
                // Determine fallback
                if (currentMode_ == AppMode::SYSTEM_INTEGRATION) {
                    analysisCombo_->setCurrentIndex(analysisCombo_->findData(static_cast<int>(Simulation::AnalysisType::TRANSIENT)));
                } else {
                    analysisCombo_->setCurrentIndex(analysisCombo_->findData(static_cast<int>(Simulation::AnalysisType::TRANSIENT)));
                }
            }
        }
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
        QString error = "Cannot run simulation.\nPossible causes:\n1. ERC Violations (Fix errors in schematic)\n2. No schematic loaded\n3. Missing Ground reference";
        appendLog("[ERROR] " + error.replace("\n", " "));
        
        // WASM-SAFE: Do NOT use blocking QMessageBox here as it may crash the browser context if invoked from async path.
        // The error is already logged to the output panel.
        // QMessageBox::warning(this, "Simulation Failed", error); 
        
        // Flash status bar red
        findChild<QLabel*>("statusLabel")->setText("Failed: ERC / Checks");
        findChild<QLabel*>("statusLabel")->setStyleSheet("color: #f44747;");
        return;
    }
    
    // Get netlist
    netlist_ = schematicEditor_->toNetlist();
    
    // Create run object
    currentRun_ = Simulation::SimulationRun();
    currentRun_.runId = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    currentRun_.profileId = profileId;
    currentRun_.topologyHash = currentTopologyHash_;
    if (runModeCombo_) {
        currentRun_.runMode = static_cast<acutesim::physics::RunMode>(runModeCombo_->currentData().toInt());
    }
    currentRun_.startTime = QDateTime::currentDateTime();
    currentRun_.status = Simulation::SimulationRun::Status::RUNNING;
    
    // Update UI
    isRunning_ = true;
    runBtn_->setEnabled(false);
    stopBtn_->setEnabled(true);
    findChild<QLabel*>("statusLabel")->setText("Running...");
    findChild<QLabel*>("statusLabel")->setStyleSheet("color: #dcdcaa;");
    
    // Clear streaming log
    // streamingLog_->clear(); // Now in MainWindow dock
    
    appendLog(QString("[%1] Starting %2...")
              .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
              .arg(profiles_[profileId].name));
    
    emit simulationStarted(profileId);
    
    // Start DC solve
    currentTime_ = 0.0;
    
    auto* profile = currentProfile();
    
    // Prepare corners (default to 1 nominal if empty)
    QList<acutesim::physics::CornerConfig> runCorners = profile->corners;
    if (runCorners.isEmpty()) {
        runCorners.append(acutesim::physics::CornerConfig::Nominal());
    }
    
    if (profile && profile->analysis == Simulation::AnalysisType::DC) {
        // Quick DC - Run all configured corners (Batch)
        appendStreamLog(QString("Starting DC Sweep (%1 corners)...").arg(runCorners.size()));
        
        bool allConverged = true;
        
        for (const auto& corner : runCorners) {
            // APPLY PVT
            sim_.applyCorner(netlist_, corner);
            
            appendStreamLog(QString("--> Corner: %1 (T=%2C, V=%3x)").arg(corner.name)
                            .arg(corner.temperatureC).arg(corner.voltageScale));
            
            SolverStep result = sim_.solveDC(netlist_);
            
            if (result.stats.converged) {
                 appendStreamLog(QString("   Converged in %1 iter").arg(result.stats.iterations));
                 // Emit results per corner (tagged)
                 QJsonObject jsonRes;
                 jsonRes["type"] = "DC";
                 jsonRes["corner"] = corner.name;
                 QJsonArray voltages;
                 for(double v : result.nodeVoltages) voltages.append(v);
                 jsonRes["voltages"] = voltages;
                 emit resultsReady(jsonRes);
            } else {
                 appendStreamLog("   FAILED");
                 allConverged = false;
                 
                 // Emit Solver Violation
                 ERC::ViolationType vType = ERC::ViolationType::SOLVER_DIVERGENCE;
                 QString detail = QString::fromStdString(result.stats.error_detail);
                 if (detail.contains("Singular Matrix", Qt::CaseInsensitive)) {
                     vType = ERC::ViolationType::SOLVER_SINGULAR_MATRIX;
                 }
                 
                 ERC::Violation v(vType, 
                     QString("Solver Failed: %1 (Corner %2)").arg(detail).arg(corner.name), 
                     QPoint(-9999, -9999));
                 emit solverViolation(v);
            }
        }
        
        currentRun_.status = allConverged ? 
            Simulation::SimulationRun::Status::CONVERGED : 
            Simulation::SimulationRun::Status::FAILED;
            
        currentRun_.endTime = QDateTime::currentDateTime();
        currentRun_.solveTime_ms = currentRun_.startTime.msecsTo(currentRun_.endTime);
        
        finishSimulation();
        
    } else {
        // Transient - Only support single corner for now
        if (runCorners.size() > 1) {
             appendLog("[WARNING] Batch Transient not supported in GUI yet. Running first corner only.");
        }
        
        // Apply FIRST corner
        sim_.applyCorner(netlist_, runCorners.first());
        appendStreamLog(QString("Corner: %1").arg(runCorners.first().name));
        
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
        
        // Emit waveform data
        QJsonObject point;
        point["type"] = "TransientPoint";
        point["time"] = currentTime_;
        QJsonArray voltages;
        for(double v : result.nodeVoltages) voltages.append(v);
        point["voltages"] = voltages;
        emit resultsReady(point);
        
        if (!result.stats.converged) {
            currentRun_.status = Simulation::SimulationRun::Status::FAILED;
            QString reason = result.stats.error_detail.empty() ? 
                "Transient step failed" : QString::fromStdString(result.stats.error_detail);
            currentRun_.failureReason = reason;
            
            // Emit Solver Violation
            ERC::ViolationType vType = ERC::ViolationType::SOLVER_DIVERGENCE;
            if (reason.contains("Singular Matrix", Qt::CaseInsensitive)) {
                vType = ERC::ViolationType::SOLVER_SINGULAR_MATRIX;
            } else if (reason.contains("Time step underflow", Qt::CaseInsensitive)) {
                 vType = ERC::ViolationType::SOLVER_TIMEOUT; // Or divergence?
            }
            
            ERC::Violation v(vType, 
                QString("Transient Failed at t=%1s: %2").arg(currentTime_).arg(reason), 
                QPoint(-9999, -9999));
            emit solverViolation(v);
            
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
    // explorer_->addRun(currentRun_); // Explorer removed
    runHistory_.prepend(currentRun_);
    
    // Update status
    bool passed = currentRun_.status == Simulation::SimulationRun::Status::CONVERGED;
    findChild<QLabel*>("statusLabel")->setText(passed ? "Passed" : "Failed");
    findChild<QLabel*>("statusLabel")->setStyleSheet(passed ? "color: #4ec9b0;" : "color: #f44747;");
    
    // Phase 1.7.9: Update Trust Badge
    if (trustBadge_) {
        TrustBadgeWidget::TrustLevel level = 
            passed ? (currentRun_.trustGminUsed || currentRun_.trustSourceStepUsed ? 
                      TrustBadgeWidget::TrustLevel::MEDIUM : 
                      TrustBadgeWidget::TrustLevel::HIGH) :
            TrustBadgeWidget::TrustLevel::LOW;
        
        trustBadge_->setTrustData(
            level,
            currentRun_.trustMethod.isEmpty() ? "Newton-Raphson" : currentRun_.trustMethod,
            currentRun_.iterations,
            currentRun_.trustMaxResidual,
            currentRun_.trustProvenance
        );
    }
    
    appendLog(QString("[%1] %2: %3 (%4 ms)")
              .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
              .arg(profiles_[currentProfileId_].name)
              .arg(passed ? "PASSED" : "FAILED")
              .arg(currentRun_.solveTime_ms, 0, 'f', 1));
    
    // Phase 7: Completion notification (non-blocking for WASM compatibility)
    // The status is already shown in the toolbar statusLabel and log panel
#ifndef __EMSCRIPTEN__
    if (passed) {
        QMessageBox::information(this, "Simulation Complete", 
            QString("Simulation PASSED in %1 ms").arg(currentRun_.solveTime_ms, 0, 'f', 1));
    } else {
        QMessageBox::warning(this, "Simulation Complete", 
            "Simulation FAILED\n\nCheck streaming log for details.");
    }
#endif
    
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
    emit logMessage(msg);
}

void SimulationWorkspace::appendStreamLog(const QString& msg) {
    emit streamMessage(msg);
}

void SimulationWorkspace::configureCorners() {
    QDialog dlg(this);
    dlg.setWindowTitle("Setup Simulation Corners");
    dlg.resize(400, 300);
    
    QVBoxLayout* layout = new QVBoxLayout(&dlg);
    
    acutesim::gui::analysis::CornerSetupWidget* setupWidget = new acutesim::gui::analysis::CornerSetupWidget(&dlg);
    layout->addWidget(setupWidget);
    
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);
    
    if (dlg.exec() == QDialog::Accepted) {
        // Apply corners to current profile
        auto* profile = currentProfile();
        if (profile) {
            profile->corners = setupWidget->getCorners().toList(); // Convert QVector to QList
            
            // If Monte Carlo, update Run Mode?
            // Actually, SimulationProfile handles the run logic.
            // Just notify user
            if (profile->corners.size() > 1) {
                appendLog(QString("[INFO] Configured %1 corners for next run.").arg(profile->corners.size()));
            } else {
                 appendLog("[INFO] Configured single corner.");
            }
        }
    }
}

// ============================================================================
// STATISTICAL ANALYSIS IMPLEMENTATION
// ============================================================================

void SimulationWorkspace::setupStatisticalDashboard() {
    if (statsDashboard_) return;
    
    statsDashboard_ = new StatisticalDashboard(this);
    
    // Connect schematic highlighting
    connect(statsDashboard_, &StatisticalDashboard::highlightComponent, this, [this](const QString& name) {
        if (schematicEditor_) {
            schematicEditor_->highlightComponent(name);
            appendLog("Highlighting component: " + name);
        }
    });

    // Connect CSV export
    connect(statsDashboard_, &StatisticalDashboard::exportRequested, this, [this]() {
        if (lastMonteCarloResults_.empty()) {
            QMessageBox::warning(this, "Export", "No Monte Carlo results available to export.");
            return;
        }

        QString fileName = QFileDialog::getSaveFileName(this, "Export Monte Carlo Results", 
                                                        "", "CSV Files (*.csv)");
        if (fileName.isEmpty()) return;

        QFile file(fileName);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << "Run,Voltage\n";
            for (size_t i = 0; i < lastMonteCarloResults_.size(); ++i) {
                out << (i + 1) << "," << lastMonteCarloResults_[i] << "\n";
            }
            file.close();
            appendLog("Exported results to: " + fileName);
        } else {
            QMessageBox::critical(this, "Export Failed", "Could not open file for writing.");
        }
    });
}

void SimulationWorkspace::runMonteCarlo() {
    if (!canRun()) return;

    // 0. Update Netlist
    netlist_ = schematicEditor_->toNetlist();

    // 1. Setup Advanced Dialog
    acutesim::gui::analysis::MonteCarloSetupDialog setupDlg(netlist_, this);
    if (setupDlg.exec() != QDialog::Accepted) return;
    
    int nRuns = setupDlg.numRuns();
    auto variations = setupDlg.variations();
    int targetNode = setupDlg.targetNode();
    auto strategy = setupDlg.strategy();
    
    appendLog(QString("Starting Monte Carlo (%1 runs, %2 vars)...").arg(nRuns).arg(variations.size()));
    

    

    
    appendLog(QString("Metric: Voltage at Node %1").arg(targetNode));
    
    // Launch Thread or Synchronous (WASM)
    // Note: We need to include "statistical_engine.h" for this to compile
    
    auto runBatch = [this, nRuns, variations, targetNode, strategy]() -> std::vector<double> {
        std::vector<double> outcomes;
        outcomes.reserve(nRuns);
        
        // Generate Scenarios
        auto scenarios = Statistics::MonteCarloRunner::generateScenarios(variations, nRuns, strategy);
        
        // Local Solver
        CircuitSim localSim;
        
        for (int i = 0; i < nRuns; ++i) {
            // Deep copy base netlist per run
            TensorNetlist runNetlist = netlist_;
            
            // Apply overrides to the globalBlock within the netlist
            Statistics::MonteCarloRunner::applyOverrides(runNetlist.globalBlock, scenarios[i]);
            
            SolverStep step = localSim.solveDC(runNetlist);
            
            if (step.stats.converged) {
                // Collect target metric
                double val = 0.0;
                if (targetNode > 0 && targetNode <= (int)step.nodeVoltages.size()) {
                    val = step.nodeVoltages[targetNode - 1]; // 0-indexed vector for 1-based nodes
                }
                outcomes.push_back(val);
            } else {
                // Failed run - treat as 0 for now
                 outcomes.push_back(0.0);
            }
        }
        return outcomes;
    };

#ifdef __EMSCRIPTEN__
    // WASM: Synchronous execution (single-threaded)
    // For better UX, we could use QTimer slicing, but for MVP synchronous is fine.
    appendLog("Running Monte Carlo synchronously (WASM limitation)...");
    
    QProgressDialog progress("Running Monte Carlo (Sync)...", "Cancel", 0, nRuns, this);
    progress.show();
    QCoreApplication::processEvents(); 
    
    std::vector<double> results = runBatch();
    lastMonteCarloResults_ = results; // Save for export
    
    // Update Dashboard
    setupStatisticalDashboard();
    if (!results.empty()) {
        double sum = std::accumulate(results.begin(), results.end(), 0.0);
        double mean = sum / results.size();
        statsDashboard_->histogram()->setData(results, mean * 0.9, mean * 1.1);
    }
    statsDashboard_->show();
    appendLog(QString("Monte Carlo completed. Processed %1 runs.").arg(results.size()));
    
#else
    // Desktop: Threaded execution
    auto* watcher = new QFutureWatcher<std::vector<double>>(this);
    connect(watcher, &QFutureWatcher<std::vector<double>>::finished, this, [this, watcher, nRuns]() {
        std::vector<double> results = watcher->result();
        
        // 4. Update Dashboard (Main Thread)
        setupStatisticalDashboard();
        
        // Calculate bounds for histogram
        if (!results.empty()) {
            lastMonteCarloResults_ = results; // Save for export
            double sum = std::accumulate(results.begin(), results.end(), 0.0);
            double mean = sum / results.size();
            statsDashboard_->histogram()->setData(results, mean * 0.9, mean * 1.1); // +/- 10%
        }
        statsDashboard_->show();
        
        appendLog(QString("Monte Carlo completed. Processed %1 runs.").arg(results.size()));
        
        watcher->deleteLater();
    });
    
    // Progress Dialog
    QProgressDialog* progress = new QProgressDialog("Running Monte Carlo Simulation...", "Cancel", 0, nRuns, this);
    progress->setWindowModality(Qt::WindowModal);
    progress->show();
    
    connect(watcher, &QFutureWatcher<std::vector<double>>::finished, progress, &QProgressDialog::reset);
    connect(progress, &QProgressDialog::canceled, watcher, &QFutureWatcher<std::vector<double>>::cancel);
    
    QFuture<std::vector<double>> future = QtConcurrent::run(runBatch);
    watcher->setFuture(future);
#endif
}

void SimulationWorkspace::runSensitivity() {
    setupStatisticalDashboard();
    
    // Mock Sensitivity Data
    std::vector<SensitivityDisplayItem> items = {
        {"R1", "resistance", -0.5},
        {"R2", "resistance", 0.5},
        {"C1", "capacitance", 0.01},
        {"Q1", "beta", 0.8},
        {"V1", "dc", 1.0}
    };
    
    statsDashboard_->pareto()->setData(items);
    statsDashboard_->show();
}

