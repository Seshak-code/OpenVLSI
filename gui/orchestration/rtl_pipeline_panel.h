#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTextEdit>
#include <QVector>
#include <QMap>

#include <optional>
#include "worker_manager.h"
#include "compute/orchestration/drivers/yosys_task.h"
#include "compute/orchestration/drivers/openroad_task.h"
#include "compute/orchestration/drivers/klayout_task.h"
#include "compute/orchestration/drivers/yosys_log_parser.h"
#include "compute/orchestration/drivers/openroad_report_parser.h"
#include "compute/orchestration/drivers/klayout_report_parser.h"

namespace acutesim::gui::orchestration {

/**
 * @brief Pipeline step status.
 */
enum class StepStatus {
    PENDING,
    RUNNING,
    COMPLETED,
    FAILED
};

/**
 * @brief Internal state for one pipeline step.
 */
struct PipelineStep {
    QString name;
    StepStatus status = StepStatus::PENDING;
    QString jobId;
    QString artifactPath;   // Output from this step (feeds next step)
    float progress = 0.0f;
};

/**
 * @brief RtlPipelinePanel - Full RTL -> GDS pipeline UI widget.
 *
 * Three collapsible sections: Synthesis, Place & Route, Sign-Off.
 * Each has configuration fields, a status indicator, and progress bar.
 * Bottom: pipeline log and Run/Cancel buttons.
 */
class RtlPipelinePanel : public QWidget {
    Q_OBJECT

public:
    explicit RtlPipelinePanel(QWidget* parent = nullptr);

signals:
    void pipelineCompleted(bool success);
    void logMessage(const QString& msg);
    void overlayRequested(const QString& defFilePath);

private slots:
    void startFullPipeline();
    void cancelPipeline();
    void onJobProgress(const QString& jobId, float progress);
    void onJobLog(const QString& jobId, const QString& line, bool isError);
    void onJobCompleted(const QString& jobId, bool success, const QString& artifactPath);
    void onExecutionModeChanged(int index);
    void onSynthesisResult(const QString& jobId, const acutesim::compute::orchestration::SynthesisResult& result);
    void onPnRResult(const QString& jobId, const acutesim::compute::orchestration::PnRResult& result);
    void onDrcResult(const QString& jobId, const acutesim::compute::orchestration::DRCResult& result);

private:
    void setupUI();
    QGroupBox* createSynthesisGroup();
    QGroupBox* createPnRGroup();
    QGroupBox* createSignOffGroup();
    QWidget* createStepHeader(int stepIndex, const QString& title);
    void updateStepUI(int stepIndex);
    void advanceToNextStep();
    void resetPipeline();

    // Pipeline state
    QVector<PipelineStep> m_steps;
    int m_currentStep = -1;
    bool m_running = false;

    // Synthesis config widgets
    QLineEdit* m_topModuleEdit = nullptr;
    QComboBox* m_targetLibCombo = nullptr;
    QComboBox* m_synthGoalCombo = nullptr;

    // Synthesis config (extra)
    QCheckBox* m_flattenCheck = nullptr;

    // PnR config widgets
    QDoubleSpinBox* m_dieWidthSpin = nullptr;
    QDoubleSpinBox* m_dieHeightSpin = nullptr;
    QDoubleSpinBox* m_coreUtilSpin = nullptr;
    QDoubleSpinBox* m_targetDensitySpin = nullptr;
    QComboBox* m_placementGoalCombo = nullptr;
    QComboBox* m_routingEffortCombo = nullptr;
    QCheckBox* m_repairAntennasCheck = nullptr;
    QLineEdit* m_clockNetEdit = nullptr;

    // Sign-Off config widgets
    QComboBox* m_checkTypeCombo = nullptr;

    // Step status UI (per step)
    QVector<QLabel*> m_stepStatusDots;
    QVector<QProgressBar*> m_stepProgressBars;
    QVector<QLabel*> m_stepStatusLabels;

    // Physical overlay
    QCheckBox* m_showOverlayCheck = nullptr;

    // Execution mode
    QComboBox* m_execModeCombo = nullptr;

    // Pipeline controls
    QPushButton* m_runPipelineBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QTextEdit* m_pipelineLog = nullptr;

    // Cached results
    std::optional<acutesim::compute::orchestration::SynthesisResult> m_lastSynthResult;
    std::optional<acutesim::compute::orchestration::PnRResult> m_lastPnRResult;
    std::optional<acutesim::compute::orchestration::DRCResult> m_lastDrcResult;

    // Worker manager reference
    WorkerManager* m_workerMgr = nullptr;
};

} // namespace acutesim::gui::orchestration
