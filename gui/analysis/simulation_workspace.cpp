#include "simulation_workspace.h"
#include "../../gui/styling/theme_manager.h"
#include "monte_carlo_setup_widget.h"
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
#include "../widgets/inline_dialog.h"
#include "../widgets/inline_progress.h"
#include <QFile>
#include <QTextStream>
#include <QDockWidget>
#include <QMainWindow>
#include "corner_setup_widget.h"
#include "../../physics/quick_analysis.h"
#include "../../physics/statistical_engine.h"
#include "statistical_dashboard.h"
#include <random>
#include <numeric>
#include <map>
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
    
    // Threading Setup
#ifdef ACUTESIM_THREADS_ENABLED
    workerThread_ = new QThread(this);
    worker_ = new SimulationWorker(); // Moved to thread
    worker_->moveToThread(workerThread_);
    
    // Lifecycle
    connect(workerThread_, &QThread::finished, worker_, &QObject::deleteLater);
#else
    worker_ = new SimulationWorker(this); // Main thread worker
#endif
    
    // Commands
    qRegisterMetaType<TensorNetlist>("TensorNetlist");
    qRegisterMetaType<Simulation::SimulationProfile>("Simulation::SimulationProfile");
    qRegisterMetaType<acutesim::physics::RunMode>("acutesim::physics::RunMode");
    qRegisterMetaType<std::shared_ptr<const CompiledTensorBlock>>("std::shared_ptr<const CompiledTensorBlock>");
    
    // Legacy TensorNetlist connections (kept for backward compatibility)
    connect(this, &SimulationWorkspace::startDCRequested, worker_, 
        static_cast<void(SimulationWorker::*)(TensorNetlist, Simulation::SimulationProfile, acutesim::physics::RunMode)>(&SimulationWorker::startDC));
    connect(this, &SimulationWorkspace::startTransientRequested, worker_, 
        static_cast<void(SimulationWorker::*)(TensorNetlist, Simulation::SimulationProfile, acutesim::physics::RunMode)>(&SimulationWorker::startTransient));
    connect(this, &SimulationWorkspace::startACRequested, worker_, 
        static_cast<void(SimulationWorker::*)(TensorNetlist, Simulation::SimulationProfile, acutesim::physics::RunMode)>(&SimulationWorker::startAC));
    connect(this, &SimulationWorkspace::startNoiseRequested, worker_, 
        static_cast<void(SimulationWorker::*)(TensorNetlist, Simulation::SimulationProfile, acutesim::physics::RunMode)>(&SimulationWorker::startNoise));
    
    // Compiled-block connections (new path)
    connect(this, &SimulationWorkspace::startDCCompiledRequested, worker_, 
        static_cast<void(SimulationWorker::*)(std::shared_ptr<const CompiledTensorBlock>, Simulation::SimulationProfile, acutesim::physics::RunMode)>(&SimulationWorker::startDC));
    connect(this, &SimulationWorkspace::startTransientCompiledRequested, worker_, 
        static_cast<void(SimulationWorker::*)(std::shared_ptr<const CompiledTensorBlock>, Simulation::SimulationProfile, acutesim::physics::RunMode)>(&SimulationWorker::startTransient));
    connect(this, &SimulationWorkspace::startACCompiledRequested, worker_, 
        static_cast<void(SimulationWorker::*)(std::shared_ptr<const CompiledTensorBlock>, Simulation::SimulationProfile, acutesim::physics::RunMode)>(&SimulationWorker::startAC));
    connect(this, &SimulationWorkspace::startNoiseCompiledRequested, worker_, 
        static_cast<void(SimulationWorker::*)(std::shared_ptr<const CompiledTensorBlock>, Simulation::SimulationProfile, acutesim::physics::RunMode)>(&SimulationWorker::startNoise));
    
    // Results
    connect(worker_, &SimulationWorker::resultsReady, this, &SimulationWorkspace::onWorkerResults);
    connect(worker_, &SimulationWorker::solverViolation, this, &SimulationWorkspace::onWorkerViolation);
    connect(worker_, &SimulationWorker::simulationFinished, this, &SimulationWorkspace::onWorkerFinished);
    connect(worker_, &SimulationWorker::logMessage, this, &SimulationWorkspace::appendLog);
    connect(worker_, &SimulationWorker::streamMessage, this, &SimulationWorkspace::appendStreamLog);
    
#ifdef ACUTESIM_THREADS_ENABLED
    workerThread_->start();
#endif
}

SimulationWorkspace::~SimulationWorkspace() {
#ifdef ACUTESIM_THREADS_ENABLED
    if (workerThread_) {
        // Explicit shutdown
        worker_->stop();
        workerThread_->quit();
        workerThread_->wait();
        // worker_ is deleted via deleteLater on finished()
    }
#endif
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
    
    // Left: Simulation Explorer (VS Code Style)
    explorer_ = new SimulationExplorer(this);
    // Explorer styling is internal, but we can style the container if needed
    mainSplitter_->addWidget(explorer_);
    
    // Connect selection signal
    connect(explorer_, &SimulationExplorer::profileSelected, this, &SimulationWorkspace::onProfileSelected);
    connect(explorer_, &SimulationExplorer::runRequested, this, &SimulationWorkspace::onRunRequested);
    
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
    // Theme Manager
    auto& tm = acutesim::gui::styling::ThemeManager::instance();
    toolbar_->setStyleSheet(QString("background-color: %1; border-bottom: 1px solid %2;")
        .arg(tm.getColor("ui.header").name())
        .arg(tm.getColor("ui.border").name()));
    
    QHBoxLayout* layout = new QHBoxLayout(toolbar_);
    layout->setContentsMargins(8, 4, 8, 4);
    
    /* profileCombo_ removed (redundant with sidebar/explorer) */
    
    layout->addSpacing(10);
    
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
        cornerCombo_->addItem(QString::fromStdString(c.name), QVariant::fromValue(c));
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
    
    layout->addSpacing(15);

    // Compute Engine Selector (Phase 2.1)
    QLabel* engineLabel = new QLabel("Engine:", toolbar_);
    engineLabel->setStyleSheet("color: #888;");
    layout->addWidget(engineLabel);

    computeEngineCombo_ = new QComboBox(toolbar_);
    computeEngineCombo_->addItem("CPU (Legacy)", static_cast<int>(ComputeEngine::LEGACY));
    computeEngineCombo_->addItem("CPU (SoA Scalar)", static_cast<int>(ComputeEngine::SOA_SCALAR));
    computeEngineCombo_->addItem("CPU (SIMD)", static_cast<int>(ComputeEngine::CPU_SIMD));
    computeEngineCombo_->addItem("GPU", static_cast<int>(ComputeEngine::GPU));
    computeEngineCombo_->addItem("GPU (Debug)", static_cast<int>(ComputeEngine::GPU_DEBUG));
    computeEngineCombo_->setMinimumWidth(130);
    computeEngineCombo_->setCurrentIndex(static_cast<int>(ComputeEngine::LEGACY)); 
    layout->addWidget(computeEngineCombo_);

    layout->addSpacing(15);
    
    // Run button
    runBtn_ = new QPushButton("Run", toolbar_);
    runBtn_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    runBtn_->setStyleSheet("QPushButton { background-color: #0e639c; color: white; padding: 4px 12px; }");
    connect(runBtn_, &QPushButton::clicked, this, &SimulationWorkspace::runCurrent);
    layout->addWidget(runBtn_);
    
    // Stop button
    stopBtn_ = new QPushButton(toolbar_);
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

        // Mode restrictions removed - all analysis types now available
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

// Helper to get help text (moved down)

void SimulationWorkspace::setContext(const acutesim::gui::core::SimulationContext& context) {
    currentContext_ = context;
    
    // Adapt UI based on context
    if (context.type == acutesim::gui::core::SimulationType::Digital_Verilog) {
        // Hide analog specific widgets or change labels?
        // For now, minimal adaptation
        findChild<QLabel*>("statusLabel")->setText("Ready (Digital)");
    } else if (context.type == acutesim::gui::core::SimulationType::Analog_SPICE) {
         findChild<QLabel*>("statusLabel")->setText("Ready (Analog)");
    } else {
         findChild<QLabel*>("statusLabel")->setText("Ready");
    }
}

void SimulationWorkspace::markTopologyChanged(size_t newHash) {
    onTopologyChanged(newHash);
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
    
    // Populate Explorer
    if (explorer_) {
        // explorer_->clear(); // TODO: Implement clear
        for (const auto& p : profiles_) {
            explorer_->addProfile(p);
        }
    }
    
    // Connect topology changes
    connect(editor, &SchematicEditor::topologyChanged, this, &SimulationWorkspace::onTopologyChanged);

    // Mode connections removed - unified component library
    updateProfileEditor();
}

void SimulationWorkspace::selectProfile(const QString& profileId) {
    if (!profiles_.contains(profileId)) return;
    
    currentProfileId_ = profileId;
    updateProfileEditor();
    
    /* profileCombo_ removed */
    
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

// onModeChanged() removed - unified component library

void SimulationWorkspace::updateAnalysisVisibility() {
    if (!analysisCombo_) return;

    QStandardItemModel* model = qobject_cast<QStandardItemModel*>(analysisCombo_->model());
    if (!model) {
        appendLog("[ERROR] analysisCombo model is not QStandardItemModel");
        return;
    }

    // Mode-based filtering removed - unified component library
    // All analysis types are now available regardless of mode

    for (int i = 0; i < analysisCombo_->count(); ++i) {
        QStandardItem* item = model->item(i);
        if (item) {
            item->setEnabled(true);
            item->setForeground(QBrush(Qt::white));
        }
    }
}

bool SimulationWorkspace::canRun() const {
    if (!schematicEditor_) {
        qWarning() << "[SimWorkspace] canRun: schematicEditor_ is NULL";
        return false;
    }

    auto* scene = schematicEditor_->getScene();
    if (!scene) {
        qWarning() << "[SimWorkspace] canRun: scene is NULL";
        return false;
    }

    // Check ERC
    if (!scene->canSimulate()) {
        qWarning() << "[SimWorkspace] canRun: ERC check failed (canSimulate=false)";
        return false;
    }

    qDebug() << "[SimWorkspace] canRun: all checks passed";
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
    qDebug() << "[SimWorkspace] onRunRequested called with profileId:" << profileId;

    if (!canRun()) {
        QString error = "Cannot run simulation.\nPossible causes:\n1. ERC Violations (Fix errors in schematic)\n2. No schematic loaded\n3. Missing Ground reference";
        appendLog("[ERROR] " + error.replace("\n", " "));
        if (auto* label = findChild<QLabel*>("statusLabel")) {
            label->setText("Failed: ERC / Checks");
            label->setStyleSheet("color: #f44747;");
        }
        return;
    }

    qDebug() << "[SimWorkspace] canRun() passed, proceeding with simulation";
    
    // Get netlist (Main Thread)
    netlist_ = schematicEditor_->toNetlist();
    
    // ── NetlistCompiler: Compile and cache ──────────────────────────────
    netlist_.globalBlock.computeTopologyHash();
    size_t currentHash = netlist_.globalBlock.topologyHash;
    
    if (!currentCompiledBlock_ || currentCompiledBlock_->topologyHash != currentHash) {
        appendLog("[Compiler] Topology changed — recompiling netlist...");
        currentCompiledBlock_ = NetlistCompiler::compile(netlist_);
        appendLog(QString("[Compiler] Compiled block: %1 nodes, hash=%2")
            .arg(currentCompiledBlock_->nodeCount)
            .arg(currentCompiledBlock_->topologyHash));
    } else {
        appendLog("[Compiler] Using cached compiled block.");
    }
    
    // Create run object logic (UI State)
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

    emit simulationStarted(profileId);

    auto* profile = currentProfile();
    Simulation::SimulationProfile profCopy = *profile; // Copy
    
    // Dispatch using compiled block
    switch (profCopy.analysis) {
        case Simulation::AnalysisType::DC:
            emit startDCCompiledRequested(currentCompiledBlock_, profCopy, currentRun_.runMode);
            break;
        case Simulation::AnalysisType::AC:
            emit startACCompiledRequested(currentCompiledBlock_, profCopy, currentRun_.runMode);
            break;
        case Simulation::AnalysisType::NOISE:
            emit startNoiseCompiledRequested(currentCompiledBlock_, profCopy, currentRun_.runMode);
            break;
        case Simulation::AnalysisType::TRANSIENT:
        default:
            emit startTransientCompiledRequested(currentCompiledBlock_, profCopy, currentRun_.runMode);
            break;
    }
}

void SimulationWorkspace::stopSimulation() {
    if (worker_) worker_->stop();
}

void SimulationWorkspace::onWorkerResults(const QJsonObject& results) {
    QString type = results.value("type").toString();

    // Per-step transient points: forward data but don't update run status yet
    // (status is set by the TransientSummary at the end, or by solverViolation on failure)
    if (type == "TransientPoint") {
        emit resultsReady(results);
        return;
    }

    // DC result or TransientSummary: mark run as converged and extract stats
    currentRun_.status = Simulation::SimulationRun::Status::CONVERGED;

    if (results.contains("stats")) {
        QJsonObject stats = results["stats"].toObject();
        currentRun_.iterations = stats.value("iterations").toInt();
        currentRun_.trustMaxResidual = stats.value("residual").toDouble();
        currentRun_.trustMethod = stats.value("method").toString();
        currentRun_.trustGminUsed = stats.value("gminSteppingUsed").toBool();
        currentRun_.trustSourceStepUsed = stats.value("sourceSteppingUsed").toBool();
    }

    emit resultsReady(results);
}

void SimulationWorkspace::onWorkerViolation(const ERC::Violation& violation) {
    currentRun_.status = Simulation::SimulationRun::Status::FAILED;
    emit solverViolation(violation);
}

void SimulationWorkspace::onWorkerFinished() {
    finishSimulation();
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
    
    // Phase 7: Completion notification (inline — works on all platforms including WASM)
    using namespace acutesim::gui::widgets;
    if (passed) {
        InlineDialog::success(this, "Simulation Complete",
            QString("Simulation PASSED in %1 ms").arg(currentRun_.solveTime_ms, 0, 'f', 1));
    } else {
        InlineDialog::warning(this, "Simulation Complete",
            "Simulation FAILED\n\nCheck streaming log for details.");
    }
    
    emit simulationCompleted(currentRun_);
}



void SimulationWorkspace::appendLog(const QString& msg) {
    emit logMessage(msg);
}

void SimulationWorkspace::appendStreamLog(const QString& msg) {
    emit streamMessage(msg);
}

void SimulationWorkspace::configureCorners() {
    using namespace acutesim::gui::widgets;
    auto* setupWidget = new acutesim::gui::analysis::CornerSetupWidget();
    InlineDialog::showCustom(this, "Setup Simulation Corners", setupWidget,
        DialogButton::Ok | DialogButton::Cancel,
        [this, setupWidget](const DialogResult& r) {
            if (r.clickedButton == DialogButton::Ok) {
                auto* profile = currentProfile();
                if (profile) {
                    profile->corners = setupWidget->getCorners().toList();
                    if (profile->corners.size() > 1) {
                        appendLog(QString("[INFO] Configured %1 corners for next run.").arg(profile->corners.size()));
                    } else {
                        appendLog("[INFO] Configured single corner.");
                    }
                }
            }
        });
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
        using namespace acutesim::gui::widgets;
        if (lastMonteCarloResults_.empty()) {
            InlineDialog::warning(this, "Export", "No Monte Carlo results available to export.");
            return;
        }

        InlineDialog::getSaveFileName(this, "Export Monte Carlo Results", "CSV Files (*.csv)",
            [this](const DialogResult& r) {
                if (r.clickedButton == DialogButton::Cancel || r.textValue.isEmpty()) return;
                QString fileName = r.textValue;
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
                    InlineDialog::critical(this, "Export Failed", "Could not open file for writing.");
                }
            });
    });
}

void SimulationWorkspace::runMonteCarlo() {
    if (!canRun()) return;

    // 0. Update Netlist
    netlist_ = schematicEditor_->toNetlist();

    // 1. Setup Advanced Dialog (inline)
    using namespace acutesim::gui::widgets;
    auto* setupWidget = new acutesim::gui::analysis::MonteCarloSetupWidget(netlist_);
    InlineDialog::showCustom(this, "Advanced Monte Carlo Setup", setupWidget,
        DialogButton::Ok | DialogButton::Cancel,
        [this, setupWidget](const DialogResult& r) {
            if (r.clickedButton != DialogButton::Ok) return;
            int nRuns = setupWidget->numRuns();
            auto variations = setupWidget->variations();
            int targetNode = setupWidget->targetNode();
            auto strategy = setupWidget->strategy();
            continueMonteCarloRun(nRuns, variations, targetNode, strategy);
        });
}

void SimulationWorkspace::continueMonteCarloRun(int nRuns,
    const std::map<std::string, Statistics::Distribution>& variations,
    int targetNode, Statistics::SamplingStrategy strategy) {
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
    appendLog("Running Monte Carlo synchronously (WASM limitation)...");

    auto* progress = new acutesim::gui::widgets::InlineProgress(this);
    progress->setTitle("Running Monte Carlo (Sync)...");
    progress->setRange(0, nRuns);
    progress->show();
    QCoreApplication::processEvents();

    std::vector<double> results = runBatch();
    lastMonteCarloResults_ = results;

    progress->dismiss();

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
    auto* progress = new acutesim::gui::widgets::InlineProgress(this);
    progress->setTitle("Running Monte Carlo Simulation...");
    progress->setRange(0, nRuns);
    progress->setCancelEnabled(true);
    progress->show();

    auto* watcher = new QFutureWatcher<std::vector<double>>(this);
    connect(watcher, &QFutureWatcher<std::vector<double>>::finished, this, [this, watcher, progress]() {
        std::vector<double> results = watcher->result();

        progress->dismiss();

        setupStatisticalDashboard();
        if (!results.empty()) {
            lastMonteCarloResults_ = results;
            double sum = std::accumulate(results.begin(), results.end(), 0.0);
            double mean = sum / results.size();
            statsDashboard_->histogram()->setData(results, mean * 0.9, mean * 1.1);
        }
        statsDashboard_->show();

        appendLog(QString("Monte Carlo completed. Processed %1 runs.").arg(results.size()));

        watcher->deleteLater();
    });

    connect(progress, &acutesim::gui::widgets::InlineProgress::cancelled, watcher, &QFutureWatcher<std::vector<double>>::cancel);

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

