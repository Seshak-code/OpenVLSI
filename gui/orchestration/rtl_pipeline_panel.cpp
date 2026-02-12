#include "rtl_pipeline_panel.h"
#include "synthesis_result_dialog.h"
#include "pnr_result_dialog.h"
#include "signoff_result_dialog.h"
#include <QScrollArea>
#include <QFormLayout>
#include <QDateTime>

namespace acutesim::gui::orchestration {

RtlPipelinePanel::RtlPipelinePanel(QWidget* parent)
    : QWidget(parent)
{
    // Initialize pipeline steps
    m_steps.resize(3);
    m_steps[0].name = "Synthesis";
    m_steps[1].name = "Place & Route";
    m_steps[2].name = "Sign-Off (DRC)";

    m_workerMgr = WorkerManager::instance();

    setupUI();

    // Connect worker manager signals
    if (m_workerMgr) {
        connect(m_workerMgr, &WorkerManager::jobProgress, this, &RtlPipelinePanel::onJobProgress);
        connect(m_workerMgr, &WorkerManager::jobLogReceived, this, &RtlPipelinePanel::onJobLog);
        connect(m_workerMgr, &WorkerManager::jobCompleted, this, &RtlPipelinePanel::onJobCompleted);
        connect(m_workerMgr, &WorkerManager::synthesisResultReady, this, &RtlPipelinePanel::onSynthesisResult);
        connect(m_workerMgr, &WorkerManager::pnrResultReady, this, &RtlPipelinePanel::onPnRResult);
        connect(m_workerMgr, &WorkerManager::drcResultReady, this, &RtlPipelinePanel::onDrcResult);

        // Enable run button when connected OR local tools available
        connect(m_workerMgr, &WorkerManager::statusChanged, this, [this](const WorkerState& state) {
            if (m_runPipelineBtn && !m_running) {
                m_runPipelineBtn->setEnabled(state.isConnected || m_workerMgr->canRunLocally());
            }
        });
    }

#ifdef __EMSCRIPTEN__
    if (m_runPipelineBtn) {
        m_runPipelineBtn->setEnabled(false);
        m_runPipelineBtn->setToolTip("Orchestration unavailable in browser");
    }
#endif
}

void RtlPipelinePanel::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    // Title
    QLabel* titleLabel = new QLabel("RTL \u2192 GDS Pipeline", this);
    titleLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #ccc; padding: 4px;");
    mainLayout->addWidget(titleLabel);

    // Scrollable content area
    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    QWidget* scrollContent = new QWidget();
    QVBoxLayout* contentLayout = new QVBoxLayout(scrollContent);
    contentLayout->setSpacing(4);

    // Step groups
    contentLayout->addWidget(createSynthesisGroup());
    contentLayout->addWidget(createPnRGroup());
    contentLayout->addWidget(createSignOffGroup());
    contentLayout->addStretch();

    scrollArea->setWidget(scrollContent);
    mainLayout->addWidget(scrollArea, 1);

    // Pipeline Log
    m_pipelineLog = new QTextEdit(this);
    m_pipelineLog->setReadOnly(true);
    m_pipelineLog->setMaximumHeight(150);
    m_pipelineLog->setStyleSheet(
        "QTextEdit { background: #1a1a2e; color: #b0b0b0; "
        "font-family: 'Menlo', 'Consolas', monospace; font-size: 11px; "
        "border: 1px solid #333; }");
    m_pipelineLog->setPlaceholderText("Pipeline log output...");
    mainLayout->addWidget(m_pipelineLog);

    // Bottom controls
    QHBoxLayout* controlLayout = new QHBoxLayout();

    // Execution mode selector
    m_execModeCombo = new QComboBox(this);
    m_execModeCombo->addItems({"Auto", "Local", "Remote", "Mock"});
    m_execModeCombo->setToolTip("Auto: use remote if connected, otherwise local");
    m_execModeCombo->setFixedWidth(90);
    connect(m_execModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RtlPipelinePanel::onExecutionModeChanged);
    controlLayout->addWidget(m_execModeCombo);

    m_runPipelineBtn = new QPushButton("Run Full Pipeline", this);
    m_runPipelineBtn->setStyleSheet(
        "QPushButton { background-color: #0e639c; color: white; padding: 6px 16px; "
        "border-radius: 3px; font-weight: bold; }"
        "QPushButton:hover { background-color: #1177bb; }"
        "QPushButton:disabled { background-color: #555; color: #888; }");
    m_runPipelineBtn->setEnabled(false); // Disabled until worker connects
    connect(m_runPipelineBtn, &QPushButton::clicked, this, &RtlPipelinePanel::startFullPipeline);
    controlLayout->addWidget(m_runPipelineBtn);

    m_cancelBtn = new QPushButton("Cancel", this);
    m_cancelBtn->setStyleSheet(
        "QPushButton { background-color: #c53030; color: white; padding: 6px 12px; "
        "border-radius: 3px; }"
        "QPushButton:disabled { background-color: #555; color: #888; }");
    m_cancelBtn->setEnabled(false);
    connect(m_cancelBtn, &QPushButton::clicked, this, &RtlPipelinePanel::cancelPipeline);
    controlLayout->addWidget(m_cancelBtn);

    controlLayout->addStretch();
    mainLayout->addLayout(controlLayout);
}

QWidget* RtlPipelinePanel::createStepHeader(int stepIndex, const QString& title) {
    QWidget* header = new QWidget();
    QHBoxLayout* layout = new QHBoxLayout(header);
    layout->setContentsMargins(0, 0, 0, 0);

    // Status dot
    QLabel* dot = new QLabel("\u25CF", header); // Filled circle
    dot->setFixedWidth(16);
    dot->setStyleSheet("color: #666; font-size: 12px;");
    layout->addWidget(dot);

    // Ensure vectors are large enough
    while (m_stepStatusDots.size() <= stepIndex) m_stepStatusDots.append(nullptr);
    while (m_stepProgressBars.size() <= stepIndex) m_stepProgressBars.append(nullptr);
    while (m_stepStatusLabels.size() <= stepIndex) m_stepStatusLabels.append(nullptr);

    m_stepStatusDots[stepIndex] = dot;

    // Title
    QLabel* titleLbl = new QLabel(QString("Step %1: %2").arg(stepIndex + 1).arg(title), header);
    titleLbl->setStyleSheet("font-weight: bold; color: #ccc;");
    layout->addWidget(titleLbl);

    // Progress bar
    QProgressBar* progress = new QProgressBar(header);
    progress->setRange(0, 100);
    progress->setValue(0);
    progress->setFixedWidth(100);
    progress->setFixedHeight(14);
    progress->setTextVisible(false);
    progress->setStyleSheet(
        "QProgressBar { background: #333; border: 1px solid #444; border-radius: 3px; }"
        "QProgressBar::chunk { background: #0e639c; border-radius: 2px; }");
    layout->addWidget(progress);
    m_stepProgressBars[stepIndex] = progress;

    // Status label
    QLabel* statusLbl = new QLabel("Pending", header);
    statusLbl->setStyleSheet("color: #888; font-size: 11px;");
    statusLbl->setFixedWidth(80);
    layout->addWidget(statusLbl);
    m_stepStatusLabels[stepIndex] = statusLbl;

    layout->addStretch();
    return header;
}

QGroupBox* RtlPipelinePanel::createSynthesisGroup() {
    QGroupBox* group = new QGroupBox("Synthesis (Yosys)", this);
    group->setCheckable(true);
    group->setChecked(true);
    group->setStyleSheet(
        "QGroupBox { color: #ccc; border: 1px solid #444; border-radius: 4px; "
        "margin-top: 8px; padding-top: 16px; }"
        "QGroupBox::title { subcontrol-origin: margin; padding: 0 6px; }");

    QVBoxLayout* layout = new QVBoxLayout(group);

    // Step header with status
    layout->addWidget(createStepHeader(0, "Synthesis"));

    // Config form
    QFormLayout* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);

    m_topModuleEdit = new QLineEdit("top", group);
    m_topModuleEdit->setStyleSheet("background: #3c3c3c; color: #ccc; padding: 3px;");
    form->addRow("Top Module:", m_topModuleEdit);

    m_targetLibCombo = new QComboBox(group);
    m_targetLibCombo->addItems({"sky130_fd_sc_hd", "sky130_fd_sc_hs", "sky130_fd_sc_ms", "nangate45"});
    form->addRow("Target Library:", m_targetLibCombo);

    m_synthGoalCombo = new QComboBox(group);
    m_synthGoalCombo->addItems({"BALANCED", "AREA", "SPEED", "POWER"});
    form->addRow("Optimization:", m_synthGoalCombo);

    m_flattenCheck = new QCheckBox("Flatten hierarchy", group);
    m_flattenCheck->setChecked(false);
    form->addRow("", m_flattenCheck);

    layout->addLayout(form);
    return group;
}

QGroupBox* RtlPipelinePanel::createPnRGroup() {
    QGroupBox* group = new QGroupBox("Place & Route (OpenROAD)", this);
    group->setCheckable(true);
    group->setChecked(true);
    group->setStyleSheet(
        "QGroupBox { color: #ccc; border: 1px solid #444; border-radius: 4px; "
        "margin-top: 8px; padding-top: 16px; }"
        "QGroupBox::title { subcontrol-origin: margin; padding: 0 6px; }");

    QVBoxLayout* layout = new QVBoxLayout(group);

    // Step header with status
    layout->addWidget(createStepHeader(1, "Place & Route"));

    QFormLayout* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);

    m_dieWidthSpin = new QDoubleSpinBox(group);
    m_dieWidthSpin->setRange(10.0, 10000.0);
    m_dieWidthSpin->setValue(200.0);
    m_dieWidthSpin->setSuffix(" \u00B5m");
    form->addRow("Die Width:", m_dieWidthSpin);

    m_dieHeightSpin = new QDoubleSpinBox(group);
    m_dieHeightSpin->setRange(10.0, 10000.0);
    m_dieHeightSpin->setValue(200.0);
    m_dieHeightSpin->setSuffix(" \u00B5m");
    form->addRow("Die Height:", m_dieHeightSpin);

    m_coreUtilSpin = new QDoubleSpinBox(group);
    m_coreUtilSpin->setRange(0.1, 0.95);
    m_coreUtilSpin->setValue(0.5);
    m_coreUtilSpin->setSingleStep(0.05);
    form->addRow("Core Utilization:", m_coreUtilSpin);

    m_targetDensitySpin = new QDoubleSpinBox(group);
    m_targetDensitySpin->setRange(0.1, 0.95);
    m_targetDensitySpin->setValue(0.6);
    m_targetDensitySpin->setSingleStep(0.05);
    form->addRow("Target Density:", m_targetDensitySpin);

    m_placementGoalCombo = new QComboBox(group);
    m_placementGoalCombo->addItems({"BALANCED", "TIMING", "CONGESTION", "POWER"});
    form->addRow("Placement Goal:", m_placementGoalCombo);

    m_routingEffortCombo = new QComboBox(group);
    m_routingEffortCombo->addItems({"MEDIUM", "LOW", "HIGH"});
    form->addRow("Routing Effort:", m_routingEffortCombo);

    m_repairAntennasCheck = new QCheckBox("Repair antenna violations", group);
    m_repairAntennasCheck->setChecked(true);
    form->addRow("", m_repairAntennasCheck);

    m_clockNetEdit = new QLineEdit("clk", group);
    m_clockNetEdit->setStyleSheet("background: #3c3c3c; color: #ccc; padding: 3px;");
    form->addRow("Clock Net:", m_clockNetEdit);

    layout->addLayout(form);

    m_showOverlayCheck = new QCheckBox("Show Physical Overlay", group);
    m_showOverlayCheck->setChecked(false);
    m_showOverlayCheck->setToolTip("Overlay DEF placement data on the active schematic");
    layout->addWidget(m_showOverlayCheck);

    return group;
}

QGroupBox* RtlPipelinePanel::createSignOffGroup() {
    QGroupBox* group = new QGroupBox("Sign-Off (KLayout)", this);
    group->setCheckable(true);
    group->setChecked(true);
    group->setStyleSheet(
        "QGroupBox { color: #ccc; border: 1px solid #444; border-radius: 4px; "
        "margin-top: 8px; padding-top: 16px; }"
        "QGroupBox::title { subcontrol-origin: margin; padding: 0 6px; }");

    QVBoxLayout* layout = new QVBoxLayout(group);

    // Step header with status
    layout->addWidget(createStepHeader(2, "Sign-Off"));

    QFormLayout* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);

    m_checkTypeCombo = new QComboBox(group);
    m_checkTypeCombo->addItems({"DRC", "LVS", "PEX"});
    form->addRow("Check Type:", m_checkTypeCombo);

    layout->addLayout(form);
    return group;
}

void RtlPipelinePanel::updateStepUI(int stepIndex) {
    if (stepIndex < 0 || stepIndex >= m_steps.size()) return;

    const auto& step = m_steps[stepIndex];
    QLabel* dot = m_stepStatusDots.value(stepIndex);
    QProgressBar* bar = m_stepProgressBars.value(stepIndex);
    QLabel* label = m_stepStatusLabels.value(stepIndex);

    if (!dot || !bar || !label) return;

    switch (step.status) {
        case StepStatus::PENDING:
            dot->setStyleSheet("color: #666; font-size: 12px;");
            bar->setValue(0);
            label->setText("Pending");
            label->setStyleSheet("color: #888; font-size: 11px;");
            break;
        case StepStatus::RUNNING:
            dot->setStyleSheet("color: #dcdcaa; font-size: 12px;");
            bar->setValue(static_cast<int>(step.progress * 100));
            label->setText("Running...");
            label->setStyleSheet("color: #dcdcaa; font-size: 11px;");
            break;
        case StepStatus::COMPLETED:
            dot->setStyleSheet("color: #4ec9b0; font-size: 12px;");
            bar->setValue(100);
            label->setText("Completed");
            label->setStyleSheet("color: #4ec9b0; font-size: 11px;");
            break;
        case StepStatus::FAILED:
            dot->setStyleSheet("color: #f44747; font-size: 12px;");
            label->setText("Failed");
            label->setStyleSheet("color: #f44747; font-size: 11px;");
            break;
    }
}

void RtlPipelinePanel::startFullPipeline() {
    if (m_running) return;
    if (!m_workerMgr || (!m_workerMgr->isConnected() && !m_workerMgr->canRunLocally())) {
        m_pipelineLog->append("[ERROR] No worker connected and no local tools available.");
        return;
    }

    resetPipeline();
    m_running = true;
    m_runPipelineBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);

    m_pipelineLog->append(QString("[%1] Starting RTL \u2192 GDS Pipeline...")
        .arg(QDateTime::currentDateTime().toString("hh:mm:ss")));

    // Start step 0 (Synthesis)
    m_currentStep = 0;
    m_steps[0].status = StepStatus::RUNNING;
    updateStepUI(0);

    // Build synthesis config from UI
    acutesim::compute::orchestration::SynthesisConfig synthConfig;
    synthConfig.topModule = m_topModuleEdit->text();
    synthConfig.targetLibrary = m_targetLibCombo->currentText();

    QString goalStr = m_synthGoalCombo->currentText();
    if (goalStr == "AREA") synthConfig.goal = acutesim::compute::orchestration::SynthesisGoal::AREA;
    else if (goalStr == "SPEED") synthConfig.goal = acutesim::compute::orchestration::SynthesisGoal::SPEED;
    else if (goalStr == "POWER") synthConfig.goal = acutesim::compute::orchestration::SynthesisGoal::POWER;
    else synthConfig.goal = acutesim::compute::orchestration::SynthesisGoal::BALANCED;

    synthConfig.flatten = m_flattenCheck->isChecked();

    QString jobId = m_workerMgr->submitSynthesis(synthConfig);
    m_steps[0].jobId = jobId;

    m_pipelineLog->append(QString("[%1] Synthesis job submitted: %2")
        .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
        .arg(jobId));
}

void RtlPipelinePanel::advanceToNextStep() {
    int nextStep = m_currentStep + 1;

    if (nextStep >= m_steps.size()) {
        // All steps done
        m_running = false;
        m_runPipelineBtn->setEnabled(m_workerMgr && (m_workerMgr->isConnected() || m_workerMgr->canRunLocally()));
        m_cancelBtn->setEnabled(false);
        m_pipelineLog->append(QString("[%1] Pipeline COMPLETED successfully!")
            .arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
        emit pipelineCompleted(true);
        return;
    }

    m_currentStep = nextStep;
    m_steps[nextStep].status = StepStatus::RUNNING;
    updateStepUI(nextStep);

    QString prevArtifact = m_steps[nextStep - 1].artifactPath;

    if (nextStep == 1) {
        // Place & Route - use synthesis output as input
        using namespace acutesim::compute::orchestration;

        FloorplanConfig fpConfig;
        fpConfig.designName = m_topModuleEdit->text();
        fpConfig.dieArea = QSizeF(m_dieWidthSpin->value(), m_dieHeightSpin->value());
        fpConfig.coreUtilization = m_coreUtilSpin->value();
        fpConfig.synthesizedNetlist = prevArtifact;

        PlacementConfig placeConfig;
        placeConfig.designName = m_topModuleEdit->text();
        placeConfig.targetDensity = m_targetDensitySpin->value();
        QString placementStr = m_placementGoalCombo->currentText();
        if (placementStr == "TIMING") placeConfig.goal = PlacementGoal::TIMING;
        else if (placementStr == "CONGESTION") placeConfig.goal = PlacementGoal::CONGESTION;
        else if (placementStr == "POWER") placeConfig.goal = PlacementGoal::POWER;
        else placeConfig.goal = PlacementGoal::BALANCED;

        CTSConfig ctsConfig;
        ctsConfig.clockNet = m_clockNetEdit->text();

        RoutingConfig routeConfig;
        routeConfig.designName = m_topModuleEdit->text();
        routeConfig.repairAntennas = m_repairAntennasCheck->isChecked();
        QString effortStr = m_routingEffortCombo->currentText();
        if (effortStr == "LOW") routeConfig.effort = RoutingEffort::LOW;
        else if (effortStr == "HIGH") routeConfig.effort = RoutingEffort::HIGH;
        else routeConfig.effort = RoutingEffort::MEDIUM;

        QString jobId = m_workerMgr->submitPlaceAndRoute(fpConfig, placeConfig, ctsConfig, routeConfig);
        m_steps[nextStep].jobId = jobId;

        m_pipelineLog->append(QString("[%1] Place & Route job submitted: %2")
            .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
            .arg(jobId));

    } else if (nextStep == 2) {
        // Sign-Off - use PnR output (GDS) as input
        using namespace acutesim::compute::orchestration;

        DRCConfig drcConfig;
        drcConfig.layoutFile = prevArtifact;

        QString jobId = m_workerMgr->submitSignOff(drcConfig);
        m_steps[nextStep].jobId = jobId;

        m_pipelineLog->append(QString("[%1] Sign-Off job submitted: %2")
            .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
            .arg(jobId));
    }
}

void RtlPipelinePanel::cancelPipeline() {
    if (!m_running) return;

    m_pipelineLog->append(QString("[%1] Pipeline cancelled by user.")
        .arg(QDateTime::currentDateTime().toString("hh:mm:ss")));

    // Cancel current job
    if (m_currentStep >= 0 && m_currentStep < m_steps.size()) {
        if (!m_steps[m_currentStep].jobId.isEmpty() && m_workerMgr) {
            m_workerMgr->cancelJob(m_steps[m_currentStep].jobId);
        }
        m_steps[m_currentStep].status = StepStatus::FAILED;
        updateStepUI(m_currentStep);
    }

    m_running = false;
    m_runPipelineBtn->setEnabled(m_workerMgr && (m_workerMgr->isConnected() || m_workerMgr->canRunLocally()));
    m_cancelBtn->setEnabled(false);
    emit pipelineCompleted(false);
}

void RtlPipelinePanel::onJobProgress(const QString& jobId, float progress) {
    for (int i = 0; i < m_steps.size(); ++i) {
        if (m_steps[i].jobId == jobId) {
            m_steps[i].progress = progress;
            updateStepUI(i);
            break;
        }
    }
}

void RtlPipelinePanel::onJobLog(const QString& jobId, const QString& line, bool isError) {
    // Find which step this belongs to
    for (int i = 0; i < m_steps.size(); ++i) {
        if (m_steps[i].jobId == jobId) {
            QString prefix = isError ? "ERR" : "LOG";
            m_pipelineLog->append(QString("[%1][%2] %3")
                .arg(m_steps[i].name, prefix, line));
            emit logMessage(QString("[%1] %2").arg(m_steps[i].name, line));
            break;
        }
    }
}

void RtlPipelinePanel::onJobCompleted(const QString& jobId, bool success, const QString& artifactPath) {
    for (int i = 0; i < m_steps.size(); ++i) {
        if (m_steps[i].jobId == jobId) {
            if (success) {
                m_steps[i].status = StepStatus::COMPLETED;
                m_steps[i].artifactPath = artifactPath;
                m_steps[i].progress = 1.0f;
                updateStepUI(i);

                m_pipelineLog->append(QString("[%1] %2 COMPLETED -> %3")
                    .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                    .arg(m_steps[i].name)
                    .arg(artifactPath.isEmpty() ? "(no artifact)" : artifactPath));

                // If PnR step completed and overlay requested, emit signal
                if (i == 1 && m_showOverlayCheck && m_showOverlayCheck->isChecked()
                    && !artifactPath.isEmpty()) {
                    emit overlayRequested(artifactPath);
                }

                // Advance to next step
                if (i == m_currentStep) {
                    advanceToNextStep();
                }
            } else {
                m_steps[i].status = StepStatus::FAILED;
                updateStepUI(i);

                m_pipelineLog->append(QString("[%1] %2 FAILED")
                    .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                    .arg(m_steps[i].name));

                // Stop pipeline
                m_running = false;
                m_runPipelineBtn->setEnabled(m_workerMgr && (m_workerMgr->isConnected() || m_workerMgr->canRunLocally()));
                m_cancelBtn->setEnabled(false);
                emit pipelineCompleted(false);
            }
            break;
        }
    }
}

void RtlPipelinePanel::resetPipeline() {
    m_currentStep = -1;
    for (int i = 0; i < m_steps.size(); ++i) {
        m_steps[i].status = StepStatus::PENDING;
        m_steps[i].jobId.clear();
        m_steps[i].artifactPath.clear();
        m_steps[i].progress = 0.0f;
        updateStepUI(i);
    }
    m_pipelineLog->clear();
    m_lastSynthResult.reset();
    m_lastPnRResult.reset();
}

void RtlPipelinePanel::onExecutionModeChanged(int index) {
    if (!m_workerMgr) return;
    switch (index) {
        case 0: // Auto
            m_workerMgr->setExecutionMode(ExecutionMode::AUTO);
            m_workerMgr->setMockMode(false);
            break;
        case 1: // Local
            m_workerMgr->setExecutionMode(ExecutionMode::LOCAL);
            m_workerMgr->setMockMode(false);
            break;
        case 2: // Remote
            m_workerMgr->setExecutionMode(ExecutionMode::REMOTE);
            m_workerMgr->setMockMode(false);
            break;
        case 3: // Mock
            m_workerMgr->setExecutionMode(ExecutionMode::LOCAL);
            m_workerMgr->setMockMode(true);
            break;
    }
    // Re-evaluate run button
    if (m_runPipelineBtn && !m_running) {
        m_runPipelineBtn->setEnabled(m_workerMgr->isConnected() || m_workerMgr->canRunLocally());
    }
}

void RtlPipelinePanel::onSynthesisResult(
    const QString& /*jobId*/,
    const acutesim::compute::orchestration::SynthesisResult& result) {
    m_lastSynthResult = result;
    m_pipelineLog->append(QString("  Synthesis: %1 cells, %2 wires, area=%3")
        .arg(result.cellCount)
        .arg(result.wireCount)
        .arg(result.estimatedArea, 0, 'f', 2));

    auto* dialog = new SynthesisResultDialog(result, this);
    dialog->show(); // Modeless
}

void RtlPipelinePanel::onPnRResult(
    const QString& /*jobId*/,
    const acutesim::compute::orchestration::PnRResult& result) {
    m_lastPnRResult = result;
    m_pipelineLog->append(QString("  PnR: area=%1, util=%2%, WNS=%3ps, routed=%4")
        .arg(result.placement.totalArea, 0, 'f', 2)
        .arg(result.placement.utilization * 100.0, 0, 'f', 1)
        .arg(result.timing.wns, 0, 'f', 1)
        .arg(result.routing.routedNets));

    auto* dialog = new PnRResultDialog(result, this);
    dialog->show(); // Modeless
}

void RtlPipelinePanel::onDrcResult(
    const QString& /*jobId*/,
    const acutesim::compute::orchestration::DRCResult& result) {
    m_lastDrcResult = result;
    m_pipelineLog->append(QString("  Sign-Off DRC: %1 violations, %2")
        .arg(result.totalViolations)
        .arg(result.cleanDRC ? "CLEAN" : "FAILED"));

    auto* dialog = new SignOffResultDialog(result, this);
    dialog->show(); // Modeless
}

} // namespace acutesim::gui::orchestration
