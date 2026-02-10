#pragma once

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
#include "simulation_worker.h"
#include "../../physics/statistical_engine.h"

#include "../editors/schematic_editor.h"
#include "../analysis/erc_engine.h"
#include "../widgets/problems_panel.h"
#include "../widgets/trust_badge_widget.h"
#ifndef __EMSCRIPTEN__
#include <QFutureWatcher>
#include <QtConcurrent>
#endif
#include <QProgressDialog>
#include "gui_types.h"
#include "circuitsim.h"
#include "statistical_dashboard.h"
#include <QDialog>
#include <QSpinBox>
#include <QFormLayout>

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
    
    // Set active simulation context
    void setContext(const acutesim::gui::core::SimulationContext& context);
    
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
    
    // New Signals for Worker
signals:
    void startDCRequested(TensorNetlist netlist, Simulation::SimulationProfile profile, acutesim::physics::RunMode mode);
    void startTransientRequested(TensorNetlist netlist, Simulation::SimulationProfile profile, acutesim::physics::RunMode mode);
    void startACRequested(TensorNetlist netlist, Simulation::SimulationProfile profile, acutesim::physics::RunMode mode);
    void startNoiseRequested(TensorNetlist netlist, Simulation::SimulationProfile profile, acutesim::physics::RunMode mode);

    
private slots:
    void onProfileSelected(const QString& profileId);
    void onRunRequested(const QString& profileId);

    // void stepSimulation(); // Removed
    void appendLog(const QString& msg);
    void appendStreamLog(const QString& msg);
    
    // Worker Slots
    void onWorkerResults(const QJsonObject& results);
    void onWorkerViolation(const ERC::Violation& violation);
    void onWorkerFinished();

    
private:
    // Layout
    QSplitter* mainSplitter_;
    
    // Left panel - Explorer (VS Code style)
    SimulationExplorer* explorer_;
    
    /* Removed old primitive selector */
    // QComboBox* schematicCombo_;
    
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
    acutesim::gui::core::SimulationContext currentContext_;
    QMap<QString, Simulation::SimulationProfile> profiles_;
    QString currentProfileId_;
    size_t currentTopologyHash_ = 0;
    // currentMode_ removed - unified component library
    
    // Simulation engine
    // CircuitSim sim_; // Moved to Worker
    TensorNetlist netlist_; 
    TensorNetlist lastNetlist_; // Cache for comparison?

    
    // Threading
    QThread* workerThread_ = nullptr;
    SimulationWorker* worker_ = nullptr;
    
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
    QWidget* createLineParam(const QString& defaultVal);
    QWidget* createComboParam(const QStringList& options, const QString& defaultVal);

    // Statistical Analysis
    void setupStatisticalDashboard();
    void runMonteCarlo();
    void continueMonteCarloRun(int nRuns,
        const std::map<std::string, Statistics::Distribution>& variations,
        int targetNode, Statistics::SamplingStrategy strategy);
    void runSensitivity();
    StatisticalDashboard* statsDashboard_ = nullptr;
    std::vector<double> lastMonteCarloResults_;
};
