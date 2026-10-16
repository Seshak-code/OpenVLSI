#pragma once

#include <QWidget>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTextEdit>
#include <QVector>
#include <QGroupBox>

#include "worker_manager.h"

namespace acutesim::gui::orchestration {

enum class StepStatus { PENDING, RUNNING, COMPLETED, FAILED };

struct PipelineStep {
    QString name;
    StepStatus status = StepStatus::PENDING;
    float progress = 0.0f;
};

/**
 * @brief RtlPipelinePanel - RTL -> GDS on IHP SG13G2 (Roadmap Phase 6).
 *
 * One flow job (WorkerManager::submitRtlToGds) shown as three steps: synthesis, place and
 * route (floorplan, PDN, placement, CTS, routing, fill), sign-off (DRC and LVS). The
 * flow's progress (0..1) decides which step is running.
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
    void onSynthesisResult(const QString& jobId, const acutesim::compute::orchestration::SynthesisResult& result);
    void onPnRResult(const QString& jobId, const acutesim::compute::orchestration::PnRResult& result);
    void onDrcResult(const QString& jobId, const acutesim::compute::orchestration::DRCResult& result);
    void onLvsResult(const QString& jobId, const acutesim::compute::orchestration::LVSResult& result);

private:
    void setupUI();
    QGroupBox* createSynthesisGroup();
    QGroupBox* createPnRGroup();
    QGroupBox* createSignOffGroup();
    QWidget* createStepHeader(int stepIndex, const QString& title);
    void updateStepUI(int stepIndex);
    void resetPipeline();
    void finish(bool success);
    void log(const QString& line);

    QVector<PipelineStep> m_steps;
    QString m_jobId;  // the running flow
    bool m_running = false;

    QLineEdit* m_topModuleEdit = nullptr;
    QLineEdit* m_sourcesEdit = nullptr;       // Verilog files, ';'-separated
    QLineEdit* m_clockNetEdit = nullptr;
    QDoubleSpinBox* m_clockPeriodSpin = nullptr;
    QDoubleSpinBox* m_coreUtilSpin = nullptr;  // percent
    QCheckBox* m_showOverlayCheck = nullptr;

    QVector<QLabel*> m_stepStatusDots;
    QVector<QProgressBar*> m_stepProgressBars;
    QVector<QLabel*> m_stepStatusLabels;

    QPushButton* m_runPipelineBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QTextEdit* m_pipelineLog = nullptr;

    WorkerManager* m_workerMgr = nullptr;
};

} // namespace acutesim::gui::orchestration
