#include "rtl_pipeline_panel.h"
#include "synthesis_result_dialog.h"
#include "pnr_result_dialog.h"
#include "signoff_result_dialog.h"
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QRegularExpression>
#include <QScrollArea>
#include <QVBoxLayout>

namespace acutesim::gui::orchestration {

namespace {
const char* kEditStyle = "background: #3c3c3c; color: #ccc; padding: 3px;";
// The flow's progress (rtl_flow.cpp): synthesis below 0.15, place and route to 0.88, then sign-off
int stepOf(float progress) { return progress < 0.15f ? 0 : progress < 0.88f ? 1 : 2; }
}  // namespace

RtlPipelinePanel::RtlPipelinePanel(QWidget* parent)
    : QWidget(parent)
{
    m_steps.resize(3);
    m_steps[0].name = "Synthesis";
    m_steps[1].name = "Place & Route";
    m_steps[2].name = "Sign-Off";

    m_workerMgr = WorkerManager::instance();
    setupUI();

    connect(m_workerMgr, &WorkerManager::jobProgress, this, &RtlPipelinePanel::onJobProgress);
    connect(m_workerMgr, &WorkerManager::jobLogReceived, this, &RtlPipelinePanel::onJobLog);
    connect(m_workerMgr, &WorkerManager::jobCompleted, this, &RtlPipelinePanel::onJobCompleted);
    connect(m_workerMgr, &WorkerManager::synthesisResultReady, this, &RtlPipelinePanel::onSynthesisResult);
    connect(m_workerMgr, &WorkerManager::pnrResultReady, this, &RtlPipelinePanel::onPnRResult);
    connect(m_workerMgr, &WorkerManager::drcResultReady, this, &RtlPipelinePanel::onDrcResult);
    connect(m_workerMgr, &WorkerManager::lvsResultReady, this, &RtlPipelinePanel::onLvsResult);

    const QString missing = m_workerMgr->rtlFlowMissing();
    m_runPipelineBtn->setEnabled(missing.isEmpty());
    m_runPipelineBtn->setToolTip(missing.isEmpty() ? "Synthesis, place and route, DRC and LVS on IHP SG13G2"
                                                   : "Not found: " + missing + " (Roadmap Phase 6 lists the install)");
}

void RtlPipelinePanel::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    QLabel* titleLabel = new QLabel("RTL \u2192 GDS Pipeline (IHP SG13G2)", this);
    titleLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #ccc; padding: 4px;");
    mainLayout->addWidget(titleLabel);

    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    QWidget* scrollContent = new QWidget();
    QVBoxLayout* contentLayout = new QVBoxLayout(scrollContent);
    contentLayout->setSpacing(4);
    contentLayout->addWidget(createSynthesisGroup());
    contentLayout->addWidget(createPnRGroup());
    contentLayout->addWidget(createSignOffGroup());
    contentLayout->addStretch();
    scrollArea->setWidget(scrollContent);
    mainLayout->addWidget(scrollArea, 1);

    m_pipelineLog = new QTextEdit(this);
    m_pipelineLog->setReadOnly(true);
    m_pipelineLog->setMaximumHeight(150);
    m_pipelineLog->setStyleSheet(
        "QTextEdit { background: #1a1a2e; color: #b0b0b0; "
        "font-family: 'Menlo', 'Consolas', monospace; font-size: 11px; "
        "border: 1px solid #333; }");
    m_pipelineLog->setPlaceholderText("Pipeline log output...");
    mainLayout->addWidget(m_pipelineLog);

    QHBoxLayout* controlLayout = new QHBoxLayout();
    m_runPipelineBtn = new QPushButton("Run Full Pipeline", this);
    m_runPipelineBtn->setStyleSheet(
        "QPushButton { background-color: #0e639c; color: white; padding: 6px 16px; "
        "border-radius: 3px; font-weight: bold; }"
        "QPushButton:hover { background-color: #1177bb; }"
        "QPushButton:disabled { background-color: #555; color: #888; }");
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
    group->setStyleSheet(
        "QGroupBox { color: #ccc; border: 1px solid #444; border-radius: 4px; "
        "margin-top: 8px; padding-top: 16px; }"
        "QGroupBox::title { subcontrol-origin: margin; padding: 0 6px; }");
    QVBoxLayout* layout = new QVBoxLayout(group);
    layout->addWidget(createStepHeader(0, "Synthesis"));

    QFormLayout* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);
    m_topModuleEdit = new QLineEdit("top", group);
    m_topModuleEdit->setStyleSheet(kEditStyle);
    form->addRow("Top Module:", m_topModuleEdit);

    QWidget* sources = new QWidget(group);
    QHBoxLayout* sourcesLayout = new QHBoxLayout(sources);
    sourcesLayout->setContentsMargins(0, 0, 0, 0);
    m_sourcesEdit = new QLineEdit(sources);
    m_sourcesEdit->setStyleSheet(kEditStyle);
    m_sourcesEdit->setPlaceholderText("a.v;b.v");
    sourcesLayout->addWidget(m_sourcesEdit, 1);
    QPushButton* browse = new QPushButton("Browse...", sources);
    connect(browse, &QPushButton::clicked, this, [this]() {
        const QStringList files = QFileDialog::getOpenFileNames(this, "Verilog sources", QString(),
                                                                "Verilog (*.v *.sv);;All files (*)");
        if (!files.isEmpty()) m_sourcesEdit->setText(files.join(';'));
    });
    sourcesLayout->addWidget(browse);
    form->addRow("Verilog:", sources);
    form->addRow("Library:", new QLabel("IHP SG13G2 sg13g2_stdcell (130 nm)", group));
    layout->addLayout(form);
    return group;
}

QGroupBox* RtlPipelinePanel::createPnRGroup() {
    QGroupBox* group = new QGroupBox("Place & Route (OpenROAD)", this);
    group->setStyleSheet(
        "QGroupBox { color: #ccc; border: 1px solid #444; border-radius: 4px; "
        "margin-top: 8px; padding-top: 16px; }"
        "QGroupBox::title { subcontrol-origin: margin; padding: 0 6px; }");
    QVBoxLayout* layout = new QVBoxLayout(group);
    layout->addWidget(createStepHeader(1, "Place & Route"));

    QFormLayout* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);
    m_clockNetEdit = new QLineEdit("clk", group);
    m_clockNetEdit->setStyleSheet(kEditStyle);
    form->addRow("Clock Port:", m_clockNetEdit);

    m_clockPeriodSpin = new QDoubleSpinBox(group);
    m_clockPeriodSpin->setRange(0.5, 1000.0);
    m_clockPeriodSpin->setValue(10.0);
    m_clockPeriodSpin->setSuffix(" ns");
    form->addRow("Clock Period:", m_clockPeriodSpin);

    m_coreUtilSpin = new QDoubleSpinBox(group);
    m_coreUtilSpin->setRange(10.0, 90.0);
    m_coreUtilSpin->setValue(40.0);
    m_coreUtilSpin->setSingleStep(5.0);
    m_coreUtilSpin->setSuffix(" %");
    m_coreUtilSpin->setToolTip("Cell area / core area; lower leaves room for routing and metal fill");
    form->addRow("Core Utilization:", m_coreUtilSpin);
    layout->addLayout(form);

    m_showOverlayCheck = new QCheckBox("Show Physical Overlay", group);
    m_showOverlayCheck->setToolTip("Overlay the routed DEF on the active schematic");
    layout->addWidget(m_showOverlayCheck);
    return group;
}

QGroupBox* RtlPipelinePanel::createSignOffGroup() {
    QGroupBox* group = new QGroupBox("Sign-Off (KLayout)", this);
    group->setStyleSheet(
        "QGroupBox { color: #ccc; border: 1px solid #444; border-radius: 4px; "
        "margin-top: 8px; padding-top: 16px; }"
        "QGroupBox::title { subcontrol-origin: margin; padding: 0 6px; }");
    QVBoxLayout* layout = new QVBoxLayout(group);
    layout->addWidget(createStepHeader(2, "Sign-Off"));
    QLabel* note = new QLabel("IHP's maximal DRC deck (global density is a chip-level check, met by IHP's filler "
                              "at chip assembly) and KLayout LVS against the routed netlist.", group);
    note->setWordWrap(true);
    note->setStyleSheet("color: #888; font-size: 11px;");
    layout->addWidget(note);
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
    using namespace acutesim::compute::orchestration;
    RtlFlowConfig cfg;
    cfg.top = m_topModuleEdit->text().trimmed().toStdString();
    for (const QString& f : m_sourcesEdit->text().split(';', Qt::SkipEmptyParts))
        cfg.verilog.push_back(f.trimmed().toStdString());
    cfg.clockPort = m_clockNetEdit->text().trimmed().toStdString();
    cfg.clockPeriodNs = m_clockPeriodSpin->value();
    cfg.coreUtilization = m_coreUtilSpin->value();
    cfg.design = QString::fromStdString(cfg.top).replace(QRegularExpression("[^A-Za-z0-9_]"), "_").toStdString();
    cfg.workDir = (QDir::homePath() + "/.openvlsi/flow").toStdString();  // make: no spaces

    resetPipeline();
    m_running = true;
    m_runPipelineBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);
    m_steps[0].status = StepStatus::RUNNING;
    updateStepUI(0);
    log(QString("Starting RTL \u2192 GDS for %1 (%2 source%3)")
            .arg(m_topModuleEdit->text()).arg(cfg.verilog.size()).arg(cfg.verilog.size() == 1 ? "" : "s"));
    m_jobId = m_workerMgr->submitRtlToGds(cfg);
}

void RtlPipelinePanel::cancelPipeline() {
    if (m_running) m_workerMgr->cancelJob(m_jobId);  // jobCompleted(false) follows
}

void RtlPipelinePanel::onJobProgress(const QString& jobId, float progress) {
    if (jobId != m_jobId) return;
    const int current = stepOf(progress);
    static const float start[] = {0.0f, 0.15f, 0.88f, 1.0f};
    for (int i = 0; i < m_steps.size(); ++i) {
        m_steps[i].status = i < current ? StepStatus::COMPLETED : i == current ? StepStatus::RUNNING : StepStatus::PENDING;
        m_steps[i].progress = i < current ? 1.0f : i == current ? (progress - start[i]) / (start[i + 1] - start[i]) : 0.0f;
        updateStepUI(i);
    }
}

void RtlPipelinePanel::onJobLog(const QString& jobId, const QString& line, bool isError) {
    // The flow logs tens of thousands of lines: the panel keeps the stages and the errors;
    // every tool's full log is under the work directory's logs/
    if (jobId != m_jobId) return;
    if (isError || line.startsWith("== ") || line.contains("ERROR")) log(isError ? "ERR " + line : line);
}

void RtlPipelinePanel::onJobCompleted(const QString& jobId, bool success, const QString& artifactPath) {
    if (jobId != m_jobId) return;
    if (success) {
        for (int i = 0; i < m_steps.size(); ++i) {
            m_steps[i].status = StepStatus::COMPLETED;
            updateStepUI(i);
        }
        log("GDS: " + artifactPath);
        const QString def = QFileInfo(artifactPath).dir().filePath("6_final.def");
        if (m_showOverlayCheck->isChecked() && QFileInfo::exists(def)) emit overlayRequested(def);
    } else {
        for (int i = 0; i < m_steps.size(); ++i)
            if (m_steps[i].status == StepStatus::RUNNING) {
                m_steps[i].status = StepStatus::FAILED;
                updateStepUI(i);
            }
    }
    finish(success);
}

void RtlPipelinePanel::finish(bool success) {
    log(success ? "Pipeline COMPLETED: timing met, DRC clean, LVS matched" : "Pipeline FAILED");
    m_running = false;
    m_jobId.clear();
    m_runPipelineBtn->setEnabled(m_workerMgr->canRunRtlFlow());
    m_cancelBtn->setEnabled(false);
    emit pipelineCompleted(success);
}

void RtlPipelinePanel::log(const QString& line) {
    m_pipelineLog->append(QString("[%1] %2").arg(QDateTime::currentDateTime().toString("hh:mm:ss"), line));
    emit logMessage(line);
}

void RtlPipelinePanel::resetPipeline() {
    for (int i = 0; i < m_steps.size(); ++i) {
        m_steps[i].status = StepStatus::PENDING;
        m_steps[i].progress = 0.0f;
        updateStepUI(i);
    }
    m_pipelineLog->clear();
}

void RtlPipelinePanel::onSynthesisResult(const QString& jobId,
                                         const acutesim::compute::orchestration::SynthesisResult& result) {
    if (jobId != m_jobId) return;
    log(QString("  Synthesis: %1 cells, %2 um\u00B2").arg(result.cellCount).arg(result.estimatedArea, 0, 'f', 1));
    (new SynthesisResultDialog(result, this))->show();  // modeless
}

void RtlPipelinePanel::onPnRResult(const QString& jobId, const acutesim::compute::orchestration::PnRResult& result) {
    if (jobId != m_jobId) return;
    log(QString("  P&R: %1 um\u00B2 at %2 %, WNS %3 ns, %4 routing DRC, %5 mW")
            .arg(result.placement.totalArea, 0, 'f', 0)
            .arg(result.placement.utilization * 100.0, 0, 'f', 0)
            .arg(result.timing.wns, 0, 'f', 3)
            .arg(result.routing.drcViolations)
            .arg(result.power.totalPower, 0, 'f', 3));
    (new PnRResultDialog(result, this))->show();
}

void RtlPipelinePanel::onDrcResult(const QString& jobId, const acutesim::compute::orchestration::DRCResult& result) {
    if (jobId != m_jobId) return;
    log(QString("  DRC (IHP maximal deck): %1 violations, %2").arg(result.totalViolations).arg(result.cleanDRC ? "CLEAN" : "FAILED"));
    (new SignOffResultDialog(result, this))->show();
}

void RtlPipelinePanel::onLvsResult(const QString& jobId, const acutesim::compute::orchestration::LVSResult& result) {
    if (jobId != m_jobId) return;
    log(QString("  LVS: %1").arg(result.matched ? "netlists match" : "MISMATCH"));
}

} // namespace acutesim::gui::orchestration
