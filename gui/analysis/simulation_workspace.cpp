#include "simulation_workspace.h"
#include "../../gui/styling/theme_manager.h"
#include "variables_panel.h"
#include "calculator_widget.h"
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
#include <QJsonDocument>
#include "../widgets/inline_dialog.h"
#include "../widgets/inline_progress.h"
#include <QFile>
#include <QTextStream>
#include <QDockWidget>
#include <QMainWindow>
#include "corner_setup_widget.h"
#include "statistical_dashboard.h"
#include "parametric_sweep_dialog.h"
#include "monte_carlo_setup_widget.h"
#include <random>
#include <numeric>
#include <map>
#include <QSpinBox>

// Orchestration Layer
#include "../../compute/orchestration/driver/local_worker_driver.h"
// Verilog-A transport (generate source from schematic netlist)
#include "../../components/veriloga_generator.h"

SimulationWorkspace::SimulationWorkspace(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    
    // Initialize default profiles
    profiles_["quick_dc"] = Simulation::SimulationProfile::quickDC();
    profiles_["transient_default"] = Simulation::SimulationProfile::transient();
    profiles_["ac_sweep_default"] = Simulation::SimulationProfile::acSweep();
    profiles_["noise_sweep_default"] = Simulation::SimulationProfile::noiseSweep();
    currentProfileId_ = "quick_dc";
    
    // ── Orchestration Layer Setup ────────────────────────────────────────
    // Create the LocalWorkerDriver (encapsulates CircuitSim and threading)
    auto localDriver = std::make_unique<acutesim::compute::orchestration::LocalWorkerDriver>();
    driverAdapter_ = new acutesim::gui::analysis::SimulationDriverAdapter(
        std::move(localDriver), this);
    
    // Connect driver adapter signals to workspace slots
    connect(driverAdapter_, &acutesim::gui::analysis::SimulationDriverAdapter::jobCompleted,
            this, &SimulationWorkspace::onDriverJobCompleted);
    connect(driverAdapter_, &acutesim::gui::analysis::SimulationDriverAdapter::jobProgress,
            this, &SimulationWorkspace::onDriverJobProgress);
    connect(driverAdapter_, &acutesim::gui::analysis::SimulationDriverAdapter::batchCompleted,
            this, &SimulationWorkspace::onDriverBatchCompleted);
    connect(driverAdapter_, &acutesim::gui::analysis::SimulationDriverAdapter::batchProgress,
            this, &SimulationWorkspace::onDriverBatchProgress);
}

SimulationWorkspace::~SimulationWorkspace() {
    // driverAdapter_ is a child QObject — cleaned up automatically
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
    
    // [NEW] Design Variables Panel (ADE Parity)
    variablesPanel_ = new acutesim::gui::analysis::VariablesPanel(rightPanel);
    // Remove dock features since it's embedded
    variablesPanel_->setFeatures(QDockWidget::NoDockWidgetFeatures);
    variablesPanel_->setTitleBarWidget(new QWidget()); // Hide title bar
    rightLayout->addWidget(variablesPanel_, 1); // Give it weight

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
    runModeCombo_->addItem("Full", static_cast<int>(acutesim::compute::orchestration::RunModeDTO::FULL));
    runModeCombo_->addItem("Quick", static_cast<int>(acutesim::compute::orchestration::RunModeDTO::QUICK));
    runModeCombo_->addItem("Resume", static_cast<int>(acutesim::compute::orchestration::RunModeDTO::RESUME));
    runModeCombo_->addItem("Incremental", static_cast<int>(acutesim::compute::orchestration::RunModeDTO::INCREMENTAL));
    runModeCombo_->setMinimumWidth(90);
    layout->addWidget(runModeCombo_);
    
    layout->addSpacing(10);
    
    // Corner selector
    QLabel* cornerLabel = new QLabel("Corner:", toolbar_);
    cornerLabel->setStyleSheet("color: #888;");
    layout->addWidget(cornerLabel);
    
    cornerCombo_ = new QComboBox(toolbar_);
    int i = 0;
    for (const auto& c : acutesim::compute::orchestration::CornerParametersDTO::standard5()) {
        cornerCombo_->addItem(QString::fromStdString(c.name), QVariant::fromValue(c.name)); // Just using name as value for now
        cornerCombo_->setItemData(i, QString("%1C, Vdd x%2").arg(c.temperature).arg(c.voltageScale), Qt::ToolTipRole);
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

    // Calculator Button (Waveform Icon)
    QPushButton* calcBtn = new QPushButton(toolbar_);
    calcBtn->setIcon(style()->standardIcon(QStyle::SP_ComputerIcon)); // Placeholder icon
    calcBtn->setToolTip("Open Calculator (Expression Builder)");
    calcBtn->setFixedWidth(30);
    connect(calcBtn, &QPushButton::clicked, this, &SimulationWorkspace::openCalculator);
    layout->addWidget(calcBtn);
    
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

        // Update help text for the selected analysis
        if (helpLabel_) {
            helpLabel_->setText(getAnalysisHelp(type));
        }

        // DC / AC / Transient / Noise each run from their own profile: picking one
        // here switches to it, so Run executes what the form shows.
        using AT = Simulation::AnalysisType;
        auto* cur = currentProfile();
        if ((type == AT::DC || type == AT::AC || type == AT::TRANSIENT || type == AT::NOISE) &&
            (!cur || cur->analysis != type)) {
            QString id;
            for (auto it = profiles_.cbegin(); it != profiles_.cend(); ++it)
                if (it->analysis == type) { id = it.key(); break; }
            if (id.isEmpty()) {
                auto p = type == AT::AC        ? Simulation::SimulationProfile::acSweep()
                       : type == AT::NOISE     ? Simulation::SimulationProfile::noiseSweep()
                       : type == AT::TRANSIENT ? Simulation::SimulationProfile::transient()
                                               : Simulation::SimulationProfile::quickDC();
                id = p.profileId;
                profiles_[id] = p;
            }
            selectProfile(id);
            return;
        }
        updateParametersForAnalysis(type);
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

void SimulationWorkspace::setContext(const acutesim::orchestration::SimulationContext& context) {
    currentContext_ = context;
    
    // Adapt UI based on context
    if (context.type == acutesim::orchestration::SimulationType::Digital) {
        // Hide analog specific widgets or change labels?
        // For now, minimal adaptation
        findChild<QLabel*>("statusLabel")->setText("Ready (Digital)");
    } else if (context.type == acutesim::orchestration::SimulationType::Analog) {
         findChild<QLabel*>("statusLabel")->setText("Ready (Analog)");
    } else {
         findChild<QLabel*>("statusLabel")->setText("Ready");
    }
    
    // Update Variables Panel
    if (variablesPanel_) {
        variablesPanel_->setContext(&simParams_);
        
        // Connect sweep signal (only once)
        disconnect(variablesPanel_, &acutesim::gui::analysis::VariablesPanel::sweepRequested, this, nullptr);
        connect(variablesPanel_, &acutesim::gui::analysis::VariablesPanel::sweepRequested, this, &SimulationWorkspace::onSweepRequested);
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
        profiles_["noise_sweep_default"] = Simulation::SimulationProfile::noiseSweep();
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
    
    // Add analysis-specific parameters
    bool showAdvanced = advancedParamsCheck_ && advancedParamsCheck_->isChecked();

    // Frequency sweep rows shared by AC and Noise (Cadence ADE: start, stop, sweep type, points)
    auto addFrequencySweep = [&](const QString& stopDefault) {
        form->addRow("Start frequency:", createLineParam("fStart", "1Hz"));
        form->addRow("Stop frequency:", createLineParam("fStop", stopDefault));
        form->addRow("Sweep type:", createComboParam("sweepType", {"dec", "log", "lin"}, "log",
                     {"Points per decade", "Logarithmic, total points", "Linear, total points"}));
        form->addRow("Points:", createLineParam("points", "200"));
    };

    // Add analysis-specific parameters
    switch (type) {
        case Simulation::AnalysisType::DC:
            // DC is usually parameter-free unless iterating options
            if (showAdvanced) {
                form->addRow("Accuracy:", createComboParam("accuracy", {"liberal", "moderate", "conservative"}, "moderate"));
                form->addRow("Max Iterations:", createLineParam("maxIter", "50"));
            } else {
                 form->addRow(new QLabel("Standard DC Analysis (Default)", parametersWidget_));
            }
            break;
            
        case Simulation::AnalysisType::TRANSIENT:
            form->addRow("Stop Time:", createLineParam("stopTime", "1ms"));
            if (showAdvanced) {
                form->addRow("Step Hint:", createLineParam("stepHint", "100ns"));
                form->addRow("Accuracy:", createComboParam("accuracy", {"liberal", "moderate", "conservative"}, "moderate"));
            }
            break;
            
        case Simulation::AnalysisType::AC:
            addFrequencySweep("1GHz");
            break;
            
        case Simulation::AnalysisType::NOISE: {
            // Pick nodes and source from the live schematic; editable in case it changed since
            QStringList nets{"gnd"}, sources;
            if (schematicEditor_) {
                const auto hier = schematicEditor_->toSchematicGraph();
                for (const auto& n : hier.topGraph.nets)
                    if (VerilogAGenerator::netIndex(hier, n.name) > 0) nets << QString::fromStdString(n.name);
                for (const auto& inst : hier.topGraph.instances)
                    if (inst.type == "VoltageSource" || inst.type == "CurrentSource")
                        sources << QString::fromStdString(inst.instanceName);
            }
            addFrequencySweep("100MHz");
            form->addRow("Positive output node:", createComboParam("outputNet", nets.mid(1),
                         nets.size() > 1 ? nets[1] : "", {}, true));
            form->addRow("Negative output node:", createComboParam("refNet", nets, "gnd", {}, true));
            form->addRow("Input source:", createComboParam("inputSource", sources,
                         sources.isEmpty() ? "" : sources[0], {}, true));
            break;
        }

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

// Helper: line edit bound to the current profile's parameter `key`
QWidget* SimulationWorkspace::createLineParam(const QString& key, const QString& defaultVal) {
    const QString id = currentProfileId_;
    auto write = [this, id, key](const QString& v) {
        if (profiles_.contains(id)) profiles_[id].parameters[key] = v;
    };
    auto* profile = currentProfile();
    QString cur = profile ? profile->parameters.value(key).toString() : QString();
    if (cur.isEmpty()) cur = defaultVal;
    write(cur);
    QLineEdit* edit = new QLineEdit(cur, parametersWidget_);
    edit->setStyleSheet("background: #3c3c3c; color: #ccc; padding: 4px;");
    connect(edit, &QLineEdit::textChanged, this, write);
    return edit;
}

// Helper: combo bound to the current profile's parameter `key`
QWidget* SimulationWorkspace::createComboParam(const QString& key, const QStringList& options,
                                               const QString& defaultVal, const QStringList& labels,
                                               bool editable) {
    const QString id = currentProfileId_;
    auto write = [this, id, key](const QString& v) {
        if (profiles_.contains(id)) profiles_[id].parameters[key] = v;
    };
    auto* profile = currentProfile();
    QString cur = profile ? profile->parameters.value(key).toString() : QString();
    if (cur.isEmpty()) cur = defaultVal;
    QComboBox* combo = new QComboBox(parametersWidget_);
    combo->setEditable(editable);
    for (int i = 0; i < options.size(); ++i)
        combo->addItem(i < labels.size() ? labels[i] : options[i], options[i]);
    const int idx = combo->findData(cur);
    if (idx >= 0) combo->setCurrentIndex(idx);
    else if (editable) combo->setEditText(cur);
    write(editable ? combo->currentText() : combo->currentData().toString());  // what is shown
    connect(combo, &QComboBox::currentTextChanged, this, [combo, write](const QString& text) {
        write(combo->isEditable() ? text : combo->currentData().toString());
    });
    return combo;
}

void SimulationWorkspace::onTopologyChanged(size_t newHash) {
    if (newHash != currentTopologyHash_) {
        currentTopologyHash_ = newHash;
        // explorer_->markAllStale(); // Explorer removed
        // QuickAnalysis::invalidate(); // Invalidate hover preview cache
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
    
    // ── Build SimulationRequestDTO (the ONLY thing we send across the boundary) ──
    using namespace acutesim::compute::orchestration;
    SimulationRequestDTO req;
    // Generate Verilog-A source (preferred transport, covers all 9 component types)
    HierarchicalSchematicDTO hier;
    req.source      = generateSimulationSource(&hier);
    // req.netlistJson = serializeSchematicToJson();  // [REMOVED] PURGED Phase D1
    req.topologyHash = currentTopologyHash_;
    // Show the exact source being sent to the driver
    updateVerilogADock(req.source.payload);
    req.sessionToken = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8).toStdString();
    // compute_mode removed from DTO (see SimulationRequestDTO.solverMode)

    auto* profile = currentProfile();
    if (!profile) return;
    Simulation::SimulationProfile profCopy = *profile;

    // Map analysis type
    switch (profCopy.analysis) {
        case Simulation::AnalysisType::DC:        req.analysis = AnalysisTypeDTO::DC; break;
        case Simulation::AnalysisType::TRANSIENT:  req.analysis = AnalysisTypeDTO::TRANSIENT; break;
        case Simulation::AnalysisType::AC:         req.analysis = AnalysisTypeDTO::AC; break;
        case Simulation::AnalysisType::NOISE:      req.analysis = AnalysisTypeDTO::NOISE; break;
        default:                                   req.analysis = AnalysisTypeDTO::DC; break;
    }

    // Marshal parameters from the profile into the DTO
    for (auto it = profCopy.parameters.begin(); it != profCopy.parameters.end(); ++it) {
        req.parameters[it.key().toStdString()] = it.value().toString().toStdString();
    }

    // Noise names its nodes and input source; the driver wants node indices from
    // the same snapshot the netlist came from. Check the setup before submitting.
    if (req.analysis == AnalysisTypeDTO::NOISE) {
        const QString src = profCopy.parameters.value("inputSource").toString().trimmed();
        const QString pos = profCopy.parameters.value("outputNet").toString().trimmed();
        const QString neg = profCopy.parameters.value("refNet", "gnd").toString().trimmed();
        const int posIdx = VerilogAGenerator::netIndex(hier, pos.toStdString());
        const int negIdx = neg.isEmpty() ? 0 : VerilogAGenerator::netIndex(hier, neg.toStdString());
        QString srcType;
        for (const auto& inst : hier.topGraph.instances)
            if (inst.instanceName == src.toStdString() &&
                (inst.type == "VoltageSource" || inst.type == "CurrentSource"))
                srcType = QString::fromStdString(inst.type);

        QString problem;
        if (src.isEmpty()) problem = "Noise needs an input source. Choose one in the Noise settings.";
        else if (srcType.isEmpty()) problem = QString("Input source \"%1\" isn't a voltage or current source in this schematic.").arg(src);
        else if (pos.isEmpty()) problem = "Noise needs a positive output node. Choose one in the Noise settings.";
        else if (posIdx <= 0) problem = QString("Positive output node \"%1\" isn't a net in this schematic.").arg(pos);
        else if (negIdx < 0) problem = QString("Negative output node \"%1\" isn't a net in this schematic.").arg(neg);
        if (!problem.isEmpty()) {
            appendLog("[ERROR] " + problem);
            if (auto* label = findChild<QLabel*>("statusLabel")) {
                label->setText("Failed: Noise setup");
                label->setStyleSheet("color: #f44747;");
            }
            return;
        }
        req.parameters["outputNode"] = std::to_string(posIdx);
        req.parameters["refNode"] = std::to_string(negIdx);
        noiseInputUnit_ = srcType == "CurrentSource" ? "A" : "V";
    }
    // AC excitation comes only from sources' AC magnitude; say so before a run of zeros
    if (req.analysis == AnalysisTypeDTO::AC) {
        bool anyAc = false;
        for (const auto& inst : hier.topGraph.instances) {
            auto it = inst.parameters.find("acmag");
            if (it != inst.parameters.end() && std::atof(it->second.c_str()) != 0.0) anyAc = true;
        }
        if (!anyAc)
            appendLog("[WARN] No source has an AC magnitude, so every AC result will be 0. "
                      "Set AC magnitude on your input source's properties.");
    }

    // Create run object logic (UI State)
    currentRun_ = Simulation::SimulationRun();
    currentRun_.runId = QString::fromStdString(req.sessionToken);
    currentRun_.profileId = profileId;
    currentRun_.topologyHash = currentTopologyHash_;
    currentRun_.startTime = QDateTime::currentDateTime();
    currentRun_.status = Simulation::SimulationRun::Status::RUNNING;

    // Clear cache for new run
    latestResults_.clear();

    // Update UI
    isRunning_ = true;
    runBtn_->setEnabled(false);
    stopBtn_->setEnabled(true);
    findChild<QLabel*>("statusLabel")->setText("Running...");
    findChild<QLabel*>("statusLabel")->setStyleSheet("color: #dcdcaa;");

    emit simulationStarted(profileId);
    appendLog(QString("[Orchestrator] Submitting %1 job (hash=0x%2)...")
              .arg(QString::fromStdString(analysisTypeToString(req.analysis)))
              .arg(req.topologyHash, 8, 16, QChar('0')));

    // ── Submit to Driver (the GUI's ONLY interaction with the physics world) ──
    currentJobId_ = driverAdapter_->submitJob(req);
    emit simulationSubmitted(currentJobId_);
}

void SimulationWorkspace::stopSimulation() {
    if (driverAdapter_ && currentJobId_ > 0) {
        driverAdapter_->cancelJob(currentJobId_);
        appendLog("[Orchestrator] Cancellation requested.");
    }
}

void SimulationWorkspace::openCalculator() {
    auto* calc = new acutesim::gui::analysis::CalculatorWidget(nullptr); // Floating window
    calc->setWindowTitle("Calculator");
    calc->resize(400, 500);
    calc->setWaveforms(latestResults_);
    calc->show();
    
    // Clean up on close
    calc->setAttribute(Qt::WA_DeleteOnClose);
}

// ============================================================================
// ORCHESTRATION LAYER HANDLERS (DTO-based, no physics types)
// ============================================================================

void SimulationWorkspace::onDriverJobCompleted(
    quint64 jobId, const acutesim::compute::orchestration::SimulationResponseDTO& response)
{
    Q_UNUSED(jobId);
    using namespace acutesim::compute::orchestration;

    if (response.success) {
        currentRun_.status = Simulation::SimulationRun::Status::CONVERGED;

        // Extract convergence telemetry
        currentRun_.iterations = response.convergence.iterations;
        currentRun_.trustMaxResidual = response.convergence.maxResidual;
        currentRun_.trustMethod = QString::fromStdString(response.convergence.method);
        currentRun_.trustGminUsed = response.convergence.gminSteppingUsed;
        currentRun_.trustSourceStepUsed = response.convergence.sourceSteppingUsed;

        // Populate waveform cache from raw DTO arrays
        latestResults_.clear();

        if (!response.timeVector.empty()) {
            // Transient/AC: multi-point results
            for (const auto& [nodeIdx, voltages] : response.nodeVoltages) {
                std::string name = "v(node" + std::to_string(nodeIdx + 1) + ")";
                acutesim::compute::postproc::Waveform w;
                w.name = name;
                w.unit = "V";
                for (size_t i = 0; i < response.timeVector.size() && i < voltages.size(); ++i) {
                    w.time.push_back(response.timeVector[i]);
                    w.values.push_back(voltages[i]);
                }
                latestResults_[name] = std::move(w);
            }
        } else if (!response.dcOperatingPoint.empty()) {
            // DC: single-point results
            double dc_time = 0.0;
            for (int i = 0; i < (int)response.dcOperatingPoint.size(); ++i) {
                std::string name = "v(node" + std::to_string(i + 1) + ")";
                acutesim::compute::postproc::Waveform w;
                w.name = name; w.unit = "V";
                w.time.push_back(dc_time);
                w.values.push_back(response.dcOperatingPoint[i]);
                latestResults_[name] = std::move(w);
            }
        }

        // Probe values
        for (const auto& [probeName, values] : response.probeValues) {
            std::string name = "v(\"" + probeName + "\")";
            acutesim::compute::postproc::Waveform w;
            w.name = name; w.unit = "V";
            if (!response.timeVector.empty()) {
                for (size_t i = 0; i < response.timeVector.size() && i < values.size(); ++i) {
                    w.time.push_back(response.timeVector[i]);
                    w.values.push_back(values[i]);
                }
            } else if (!values.empty()) {
                w.time.push_back(0.0);
                w.values.push_back(values[0]);
            }
            latestResults_[name] = std::move(w);
        }

        // Noise: output and input-referred spectral densities vs frequency
        if (response.extended.hasNoise) {
            const auto& nz = response.extended.noise;
            auto addNoise = [&](const std::string& name, const std::string& unit,
                                const std::vector<double>& values) {
                acutesim::compute::postproc::Waveform w;
                w.name = name;
                w.unit = unit;
                w.time = nz.frequencies;
                w.values = values;
                latestResults_[name] = std::move(w);
            };
            const std::string inUnit = noiseInputUnit_.toStdString();
            addNoise("onoise", "V/sqrt(Hz)", nz.onoise);
            addNoise("inoise", inUnit + "/sqrt(Hz)", nz.inoise);
            appendLog(QString("[Noise] Total output noise %1 V rms; input-referred %2 %3 rms")
                      .arg(nz.onoiseTotal, 0, 'g', 4).arg(nz.inoiseTotal, 0, 'g', 4)
                      .arg(noiseInputUnit_));
        }

        // Build a JSON summary for downstream consumers (waveform widget, etc.)
        QJsonObject jsonSummary;
        jsonSummary["type"] = "DriverResult";
        jsonSummary["success"] = true;
        QJsonObject stats;
        stats["iterations"] = response.convergence.iterations;
        stats["residual"] = response.convergence.maxResidual;
        stats["converged"] = true;
        stats["method"] = QString::fromStdString(response.convergence.method);
        stats["gminSteppingUsed"] = response.convergence.gminSteppingUsed;
        stats["sourceSteppingUsed"] = response.convergence.sourceSteppingUsed;
        jsonSummary["stats"] = stats;
        emit resultsReady(jsonSummary);

        // Phase 2.9: Surface solver warnings even on successful solve
        const auto& conv = response.convergence;
        if (conv.rankDeficient) {
            QString msg = QString("Near-singular matrix detected (pivot=%1 at node %2). "
                                  "Check for floating nodes or degenerate topology.")
                .arg(conv.smallestPivot, 0, 'e', 3)
                .arg(conv.rankDeficientNode + 1); // 1-based display
            ERC::Violation v(ERC::ViolationType::SOLVER_SINGULAR_MATRIX, msg, QPoint(-9999, -9999));
            emit solverViolation(v);
        }
        if (conv.divergenceDetected) {
            QString msg = QString("Solver divergence detected during NR iterations "
                                  "(worst node=%1, V=%2 V, max KCL=%3 A).")
                .arg(conv.worstNodeIndex)
                .arg(conv.worstNodeVoltage, 0, 'g', 6)
                .arg(conv.maxResidual, 0, 'e', 3);
            ERC::Violation v(ERC::ViolationType::SOLVER_DIVERGENCE, msg, QPoint(-9999, -9999));
            emit solverViolation(v);
        } else if (conv.stagnationDetected) {
            QString msg = QString("Solver stagnation detected (residual plateaued at %1 A, "
                                  "worst node=%2).")
                .arg(conv.maxResidual, 0, 'e', 3)
                .arg(conv.worstNodeIndex);
            ERC::Violation v(ERC::ViolationType::SOLVER_SLOW_CONVERGENCE, msg, QPoint(-9999, -9999));
            emit solverViolation(v);
        }
        if (conv.gminApplied > 1e-12 && conv.gminInjections > 0) {
            QString msg = QString("GMIN regularization applied (gmin=%1, nodes=%2). "
                                  "Consider adding explicit DC paths to floating nets.")
                .arg(conv.gminApplied, 0, 'e', 2)
                .arg(conv.gminInjections);
            // INFO level — use NEAR_MISS as closest INFO violation type
            ERC::Violation v(ERC::ViolationType::NEAR_MISS, msg, QPoint(-9999, -9999));
            emit solverViolation(v);
        }

    } else {
        currentRun_.status = Simulation::SimulationRun::Status::FAILED;
        QString errorMsg = QString::fromStdString(
            response.errorMessage.empty() ? response.errorDetail : response.errorMessage);
        appendLog("[Orchestrator] Solver failed: " + errorMsg);

        // Phase 2.9: Use specific violation type based on convergence telemetry
        const auto& conv = response.convergence;
        if (conv.rankDeficient) {
            ERC::Violation v(ERC::ViolationType::SOLVER_SINGULAR_MATRIX, errorMsg, QPoint(-9999, -9999));
            emit solverViolation(v);
        } else if (conv.divergenceDetected) {
            ERC::Violation v(ERC::ViolationType::SOLVER_DIVERGENCE, errorMsg, QPoint(-9999, -9999));
            emit solverViolation(v);
        } else {
            ERC::Violation v(ERC::ViolationType::SOLVER_DIVERGENCE, errorMsg, QPoint(-9999, -9999));
            emit solverViolation(v);
        }
    }

    finishSimulation();
}

void SimulationWorkspace::onDriverJobProgress(
    quint64 jobId, float percent, const QString& message)
{
    Q_UNUSED(jobId);
    appendStreamLog(QString("[%1%] %2").arg(int(percent * 100)).arg(message));
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

void SimulationWorkspace::onSweepRequested() {
    if (!canRun()) return;

    using namespace acutesim::gui::widgets;
    using namespace acutesim::compute::orchestration;
    
    // Auto-detect sweepable components
    auto netlist = schematicEditor_->toNetlist();
    QStringList sweepableVars;
    for (const auto& r : netlist.globalBlock.resistors) sweepableVars << QString::fromStdString(r.name);
    for (const auto& c : netlist.globalBlock.capacitors) sweepableVars << QString::fromStdString(c.name);
    
    if (sweepableVars.isEmpty()) {
        InlineDialog::warning(this, "Parametric Sweep", "No sweepable components found in schematic.");
        return;
    }

    auto* sweepDialog = new acutesim::gui::analysis::ParametricSweepDialog(sweepableVars);
    InlineDialog::showCustom(this, "Parametric Sweep Setup", sweepDialog,
        DialogButton::Ok | DialogButton::Cancel,
        [this, sweepDialog](const DialogResult& r) {
            if (r.clickedButton == DialogButton::Ok) {
                auto sweepParam = sweepDialog->getSweepParameter();
                auto points = sweepParam.generatePoints();
                
                SimulationBatchRequestDTO batchReq;
                batchReq.source          = generateSimulationSource();
                // batchReq.baseNetlistJson = serializeSchematicToJson();  // [REMOVED] PURGED Phase D1

                auto* profile = currentProfile();
                batchReq.analysis = AnalysisTypeDTO::DC;
                if (profile) {
                    switch (profile->analysis) {
                        case Simulation::AnalysisType::DC:        batchReq.analysis = AnalysisTypeDTO::DC; break;
                        case Simulation::AnalysisType::TRANSIENT: batchReq.analysis = AnalysisTypeDTO::TRANSIENT; break;
                        case Simulation::AnalysisType::AC:        batchReq.analysis = AnalysisTypeDTO::AC; break;
                        default: break;
                    }
                }
                batchReq.sessionToken = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8).toStdString();
                
                uint32_t runIdx = 0;
                for (double val : points) {
                    BatchRunDTO run;
                    run.runIndex = runIdx++;
                    
                    ParameterVariationDTO var;
                    var.componentName = sweepParam.variableName;
                    if (var.componentName.rfind("R", 0) == 0) var.propertyName = "resistance";
                    else if (var.componentName.rfind("C", 0) == 0) var.propertyName = "capacitance";
                    var.overrideValue = val;
                    
                    run.overrides.push_back(var);
                    batchReq.runs.push_back(run);
                }
                
                currentJobId_ = driverAdapter_->submitBatchJob(batchReq);
                emit simulationSubmitted(currentJobId_);
                appendLog(QString("[Orchestrator] Submitted parametric sweep patch job %1 with %2 runs.")
                            .arg(currentJobId_).arg(points.size()));
            }
        });
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

    using namespace acutesim::compute::orchestration;
    using namespace acutesim::gui::widgets;

    if (!schematicEditor_) {
        InlineDialog::warning(this, "Monte Carlo", "No schematic loaded.");
        return;
    }

    auto netlist = schematicEditor_->toNetlist();
    if (netlist.globalBlock.resistors.empty() && netlist.globalBlock.capacitors.empty()) {
        InlineDialog::warning(this, "Monte Carlo", "No components with tolerances found in schematic.");
        return;
    }

    static acutesim::compute::orchestration::HierarchicalSchematicDTO emptyDto;
    auto* setupWidget = new acutesim::gui::analysis::MonteCarloSetupWidget(emptyDto);
    InlineDialog::showCustom(this, "Monte Carlo Setup", setupWidget,
        DialogButton::Ok | DialogButton::Cancel,
        [this, setupWidget](const DialogResult& r) {
            if (r.clickedButton != DialogButton::Ok) return;

            using namespace acutesim::compute::orchestration;

            MonteCarloConfigDTO mcConfig;
            mcConfig.sampleCount = static_cast<uint32_t>(setupWidget->numRuns());
            mcConfig.seed = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch());
            mcConfig.variations = setupWidget->variations();

            SimulationRequestDTO baseReq;
            baseReq.source      = generateSimulationSource();
            // baseReq.netlistJson = serializeSchematicToJson();  // [REMOVED] PURGED Phase D1
            baseReq.topologyHash  = currentTopologyHash_;
            baseReq.sessionToken  = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8).toStdString();

            auto* profile = currentProfile();
            baseReq.analysis = AnalysisTypeDTO::DC;
            if (profile) {
                switch (profile->analysis) {
                    case Simulation::AnalysisType::DC:        baseReq.analysis = AnalysisTypeDTO::DC;        break;
                    case Simulation::AnalysisType::TRANSIENT: baseReq.analysis = AnalysisTypeDTO::TRANSIENT; break;
                    case Simulation::AnalysisType::AC:        baseReq.analysis = AnalysisTypeDTO::AC;        break;
                    default: break;
                }
            }

            currentJobId_ = driverAdapter_->submitStatisticalJob(baseReq, mcConfig);
            emit simulationSubmitted(currentJobId_);
            appendLog(QString("[Orchestrator] Submitted Monte Carlo job %1 with %2 samples, %3 variations.")
                        .arg(currentJobId_)
                        .arg(mcConfig.sampleCount)
                        .arg(static_cast<int>(mcConfig.variations.size())));
        });
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

// ============================================================================
// NETLIST SERIALIZATION (GUI -> JSON for the DTO)
// ============================================================================
// This method converts the SchematicEditor's internal state into a JSON string
// that the Orchestration Layer can deserialize into a TensorNetlist.
// The GUI ONLY knows how to produce JSON. It does NOT know TensorNetlist.

std::string SimulationWorkspace::serializeSchematicToJson() {
    if (!schematicEditor_) return "{}";

    auto hierDto = schematicEditor_->toSchematicGraph();
    
    QJsonObject root;
    // Flattened JSON for legacy compat (SimulationRequestDTO still expects this)
    // In D1 we will move to strictly using 'source' (Verilog-A)
    
    QJsonObject blockJson;
    QJsonArray resistors, vsources, caps, inductors;

    for (const auto& inst : hierDto.topGraph.instances) {
        if (inst.type == "Resistor") {
            QJsonObject r;
            r["name"] = QString::fromStdString(inst.instanceName);
            if (inst.connectedNets.size() >= 2) {
                r["nodeA"] = QString::fromStdString(inst.connectedNets[0]); // Using names for now, driver must resolve
                r["nodeB"] = QString::fromStdString(inst.connectedNets[1]);
            }
            resistors.append(r);
        }
        // ... more mappings if legacy JSON is still strictly required by the solver ...
        // Actually, the goal is D1 (JSON Legacy Purge), so we should minimize work here 
        // and focus on getting the Verilog-A path working perfectly.
    }
    
    // For now, return the DTO serialized as a proxy for the 'netlistJson'
    // This allows us to keep the SimulationRequestDTO interface but with DTO data.
    QJsonDocument doc(root);
    return doc.toJson(QJsonDocument::Compact).toStdString();
}

// ============================================================================
// VERILOG-A TRANSPORT (preferred netlist transport — fixes the 4/9 gap)
// ============================================================================

acutesim::compute::orchestration::SimulationSourceDTO
SimulationWorkspace::generateSimulationSource(
    acutesim::compute::orchestration::HierarchicalSchematicDTO* hierOut) {
    using namespace acutesim::compute::orchestration;
    SimulationSourceDTO src;

    if (!schematicEditor_) {
        // No editor — fall back to empty JSON source
        src.format  = NetlistFormatDTO::JSON;
        src.payload = "{}";
        return src;
    }

    auto hierDto = schematicEditor_->toSchematicGraph();
    VerilogAGenerator::GeneratorOptions opts;
    opts.topModuleName = "top";
    opts.includeProbes = true;
    src.format  = NetlistFormatDTO::VERILOG_A;
    src.payload = VerilogAGenerator::generate(hierDto, opts);
    if (hierOut) *hierOut = std::move(hierDto);
    return src;
}

void SimulationWorkspace::updateVerilogADock(const std::string& verilogASource) {
    if (!verilogADock_) {
        // Create lazily as a floating window so it doesn't disturb the layout
        verilogADock_ = new acutesim::gui::editors::CodeEditor(
            QString("__veriloga_preview__"), nullptr);
        verilogADock_->setWindowTitle("Verilog-A — Simulation Source");
        verilogADock_->setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint);
        verilogADock_->setLanguage(CodeSyntaxHighlighter::Language::VERILOG_AMS);
        verilogADock_->resize(700, 500);
        // Read-only: underlying QPlainTextEdit can be made read-only via the
        // InternalCodeEditor (the widget accepts setReadOnly on the textEdit_
        // child); we achieve this by not providing an edit path from outside.
    }
    // ponytail: the highlighted editor loads on the GUI thread (~0.6 s per 2.5 MB),
    // so large designs preview only the head; an on-demand full view is the upgrade.
    constexpr size_t kPreviewBytes = 256 * 1024;
    if (verilogASource.size() <= kPreviewBytes) {
        verilogADock_->setContent(QString::fromStdString(verilogASource));
    } else {
        const size_t cut = verilogASource.rfind('\n', kPreviewBytes);
        verilogADock_->setContent(
            QString::fromStdString(verilogASource.substr(0, cut == std::string::npos ? kPreviewBytes : cut))
            + QString("\n// ... preview truncated: %1 MB sent to the simulator ...\n")
                  .arg(verilogASource.size() / 1048576.0, 0, 'f', 1));
    }
    verilogADock_->show();
    verilogADock_->raise();
}

void SimulationWorkspace::onDriverBatchCompleted(quint64 jobId, const acutesim::compute::orchestration::SimulationBatchResponseDTO& response) {
    if (jobId != currentJobId_) return;

    appendLog(QString("[Orchestrator] Batch job %1 completed with %2 runs. Failures: %3")
              .arg(jobId).arg(response.runResults.size()).arg(response.hadFailures ? "Yes" : "No"));

    // If statistical summary is present, this was a Monte Carlo run — feed
    // the per-run probe values to the histogram and show the dashboard.
    if (!response.statistics.mean.empty()) {
        setupStatisticalDashboard();

        // Collect first-probe values across all successful runs
        lastMonteCarloResults_.clear();
        lastMonteCarloResults_.reserve(response.runResults.size());

        // Use the first probe that appears in statistics
        std::string probeName;
        if (!response.statistics.mean.empty()) {
            probeName = response.statistics.mean.begin()->first;
        }

        for (const auto& run : response.runResults) {
            if (!run.success) continue;
            auto it = run.probeValues.find(probeName);
            if (it != run.probeValues.end() && !it->second.empty()) {
                lastMonteCarloResults_.push_back(it->second[0]);
            }
        }

        if (!lastMonteCarloResults_.empty() && statsDashboard_) {
            double mean = response.statistics.mean.count(probeName)
                ? response.statistics.mean.at(probeName) : 0.0;
            double stddev = response.statistics.stddev.count(probeName)
                ? response.statistics.stddev.at(probeName) : 0.0;
            // Use ±3σ as default spec window
            statsDashboard_->histogram()->setData(
                lastMonteCarloResults_,
                mean - 3.0 * stddev,
                mean + 3.0 * stddev);
            statsDashboard_->show();
        }

        appendLog(QString("[MC] Probe '%1': mean=%2 stddev=%3 (N=%4 successful runs)")
                    .arg(QString::fromStdString(probeName))
                    .arg(response.statistics.mean.count(probeName)
                         ? response.statistics.mean.at(probeName) : 0.0, 0, 'g', 4)
                    .arg(response.statistics.stddev.count(probeName)
                         ? response.statistics.stddev.at(probeName) : 0.0, 0, 'g', 3)
                    .arg(static_cast<int>(lastMonteCarloResults_.size())));
    }

    // Show the first successful run result on the waveform canvas
    for (const auto& run : response.runResults) {
        if (run.success) {
            onDriverJobCompleted(jobId, run);
            return;
        }
    }
    finishSimulation();
}

void SimulationWorkspace::onDriverBatchProgress(quint64 jobId, quint32 completedRuns, quint32 totalRuns) {
    if (jobId != currentJobId_) return;
    
    float pct = (float)completedRuns / (float)totalRuns;
    if (auto* bar = findChild<QProgressBar*>("simProgress")) {
        bar->setValue(static_cast<int>(pct * 100));
    }
    if (auto* label = findChild<QLabel*>("statusLabel")) {
        label->setText(QString("Batch: %1 / %2 runs").arg(completedRuns).arg(totalRuns));
    }
}
