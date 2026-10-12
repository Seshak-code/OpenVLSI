#pragma once

#include <string>
#include <unordered_map>
#include <QWidget>
#include <QThread>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTimer>
#include <QStandardItemModel>
#include "simulation_explorer.h"
#include "simulation_profile.h"
#include "../core/sim_auto_detector.h"

// ── ORCHESTRATION LAYER (DTO boundary — NO physics includes) ────────────────
#include "simulation_driver_adapter.h"
#include "engine_api/simulation_dto.h"

#include "../editors/schematic/schematic_editor.h"
#include "../editors/code_editor.h"
#include "../analysis/erc_engine.h"
#include "../widgets/problems_panel.h"
#include "../widgets/trust_badge_widget.h"
#include "../../engine_api/postproc_types.h" // For caching results
#ifndef __EMSCRIPTEN__
#include <QFutureWatcher>
#include <QtConcurrent>
#endif
#include <QProgressDialog>
#include "gui_types.h"
#include "statistical_dashboard.h"
#include "variables_panel.h"
#include <QDialog>
#include <QSpinBox>
#include <QFormLayout>

class SimulationExplorer;
class CornerSetupWidget;
class StatisticalDashboard;

/**
 * SimulationWorkspace - Main dockable simulation environment
 * 
 * Replaces SimulatorWindow with a VS Code-style workspace layout:
 * - Left: Simulation Explorer (profiles, history)
 * - Center: Profile Editor (parameters)
 * - Bottom: Output tabs (Log, Streaming, Results, Waveforms)
 */
class SimulationWorkspace : public QWidget {
    Q_OBJECT
    
public:
    enum class ComputeEngine {
        LEGACY = 0,      // AoS solver, reference only
        SOA_SCALAR = 1,  // Tensor MNA on CPU, scalar
        CPU_SIMD = 2,    // Tensor MNA on CPU, vectorized
        GPU = 3,         // Primary GPU path
        GPU_DEBUG = 4    // GPU compute + extra verification
    };


    explicit SimulationWorkspace(QWidget* parent = nullptr);
    ~SimulationWorkspace();

    // Accessor for the driver adapter (allows MainWindow to wire engine event signals)
    acutesim::gui::analysis::SimulationDriverAdapter* driverAdapter() const { return driverAdapter_; }
    
    // Set active simulation context
    void setContext(const acutesim::orchestration::SimulationContext& context);
    
    // Connection to schematic (Legacy wrapper)
    void setSchematicEditor(SchematicEditor* editor);
    
    // Profile management
    void selectProfile(const QString& profileId);
    Simulation::SimulationProfile* currentProfile();
    
    // Topology invalidation
    void onTopologyChanged(size_t newHash);
    
    // Mark topology changed manually (for Unified Editor)
    void markTopologyChanged(size_t newHash);
    
signals:
    void simulationStarted(const QString& profileId);
    void simulationCompleted(const Simulation::SimulationRun& run);
    void navigateToNode(int nodeId);
    
    // Log messages for MainWindow bottom panels
    void logMessage(const QString& msg);
    void streamMessage(const QString& msg);
    void resultsReady(const QJsonObject& results);
    void solverViolation(const ERC::Violation& violation);
    
public slots:
    void runCurrent();
    void runQuickDC();
    void stopSimulation();
    void configureCorners();
    void openCalculator(); // [NEW] ViVA Parity
    
    // ── Orchestration Layer signals (DTO-based, no physics types) ────────
signals:
    void simulationSubmitted(quint64 jobId);

    
private slots:
    void onProfileSelected(const QString& profileId);
    void onRunRequested(const QString& profileId);
    void onSweepRequested(); // [NEW]

    void appendLog(const QString& msg);
    void appendStreamLog(const QString& msg);
    
    // Orchestration Layer Slots
    void onDriverJobCompleted(quint64 jobId, const acutesim::compute::orchestration::SimulationResponseDTO& response);
    void onDriverJobProgress(quint64 jobId, float percent, const QString& message);
    void onDriverBatchCompleted(quint64 jobId, const acutesim::compute::orchestration::SimulationBatchResponseDTO& response);
    void onDriverBatchProgress(quint64 jobId, quint32 completedRuns, quint32 totalRuns);

    
private:
    // Layout
    QSplitter* mainSplitter_;
    
    // Left panel - Explorer    // Docks / Sub-widgets
    SimulationExplorer* explorer_;
    QTabWidget* outputTabs_;
    CornerSetupWidget* cornersWidget_;
    acutesim::gui::analysis::VariablesPanel* variablesPanel_; // [NEW] ADE Parity
    
    // Output Widgets
    // QComboBox* schematicCombo_; // Removed old primitive selector
    
    // Center panel
    QWidget* profileEditor_;
    QLabel* profileNameLabel_;
    QComboBox* analysisCombo_;
    QCheckBox* advancedParamsCheck_;
    QWidget* parametersWidget_;
    QLabel* helpLabel_;
    
    // Toolbar
    QWidget* toolbar_;
    QPushButton* runBtn_;
    QPushButton* stopBtn_;
    // profileCombo_ removed (redundant with sidebar)
    QComboBox* runModeCombo_;
    QComboBox* cornerCombo_;
    QComboBox* computeEngineCombo_;
    TrustBadgeWidget* trustBadge_ = nullptr;
    
    // State
    SchematicEditor* schematicEditor_ = nullptr;
    acutesim::orchestration::SimulationContext currentContext_;
    acutesim::orchestration::SimulationContext simParams_; // Design variables / simulation parameters
    QMap<QString, Simulation::SimulationProfile> profiles_;
    QString noiseInputUnit_ = "V";  // "V" or "A": unit of input-referred noise for the running job
    QString currentProfileId_;
    size_t currentTopologyHash_ = 0;
    // currentMode_ removed - unified component library
    
    // ── Orchestration Layer (GUI owns ONLY the adapter, never CircuitSim) ──
    acutesim::gui::analysis::SimulationDriverAdapter* driverAdapter_ = nullptr;
    quint64 currentJobId_ = 0;
    
    // QTimer* simTimer_; // Removed
    // double currentTime_ = 0.0;
    // double timeStep_ = 1e-5;

    bool isRunning_ = false;
    
    // Run tracking
    Simulation::SimulationRun currentRun_;
    QList<Simulation::SimulationRun> runHistory_;
    
    void setupUI();
    void setupToolbar();
    void setupProfileEditor();
    void updateProfileEditor();
    void updateParametersForAnalysis(Simulation::AnalysisType type);
    void finishSimulation();
    bool canRun() const;
    void updateAnalysisVisibility();
    QString getAnalysisHelp(Simulation::AnalysisType type) const;
    
    // Parameter widget helpers
    // Editors bound to the current profile's parameter `key`: they show its value
    // (writing defaultVal if unset, so what is shown is what runs) and write edits back.
    QWidget* createLineParam(const QString& key, const QString& defaultVal);
    // options are the stored values; labels (optional) are what the list shows.
    // editable lets the user type a value that is not in the list.
    QWidget* createComboParam(const QString& key, const QStringList& options, const QString& defaultVal,
                              const QStringList& labels = {}, bool editable = false);

    // Statistical Analysis
    void setupStatisticalDashboard();
    void runMonteCarlo();
    void runSensitivity();
    StatisticalDashboard* statsDashboard_ = nullptr;
    std::vector<double> lastMonteCarloResults_;

    // Netlist JSON serialization (produces JSON for the DTO, NOT physics types)
    [[deprecated("Use generateSimulationSource() instead — migrating to Verilog-A (Phase C4)")]]
    std::string serializeSchematicToJson();

    // [NEW] Verilog-A transport — preferred over JSON; populates both fields.
    // hierOut (optional) receives the schematic snapshot the source was generated from
    acutesim::compute::orchestration::SimulationSourceDTO generateSimulationSource(
        acutesim::compute::orchestration::HierarchicalSchematicDTO* hierOut = nullptr);

    // [NEW] Read-only Verilog-A dock showing the exact source sent to driver
    acutesim::gui::editors::CodeEditor* verilogADock_ = nullptr;
    void updateVerilogADock(const std::string& verilogASource);

    // [NEW] Result Cache for Calculator
    std::unordered_map<std::string, acutesim::compute::postproc::Waveform> latestResults_;
};
