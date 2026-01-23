#pragma once

#include <QWidget>
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
#include "../editors/schematic_editor.h"
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
    explicit SimulationWorkspace(QWidget* parent = nullptr);
    
    // Connection to schematic
    void setSchematicEditor(SchematicEditor* editor);
    
    // Profile management
    void selectProfile(const QString& profileId);
    Simulation::SimulationProfile* currentProfile();
    
    // Topology invalidation
    void onTopologyChanged(size_t newHash);
    void onModeChanged(AppMode mode);
    
signals:
    void simulationStarted(const QString& profileId);
    void simulationCompleted(const Simulation::SimulationRun& run);
    void navigateToNode(int nodeId);
    
    // Log messages for MainWindow bottom panels
    void logMessage(const QString& msg);
    void streamMessage(const QString& msg);
    void resultsReady(const QJsonObject& results);
    
public slots:
    void runCurrent();
    void runQuickDC();
    void stopSimulation();
    void configureCorners();

    
private slots:
    void onProfileSelected(const QString& profileId);
    void onRunRequested(const QString& profileId);
    void stepSimulation();
    void appendLog(const QString& msg);
    void appendStreamLog(const QString& msg);
    
private:
    // Layout
    QSplitter* mainSplitter_;
    
    // Left panel - Schematic selector
    QComboBox* schematicCombo_;
    
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
    QComboBox* profileCombo_;
    QComboBox* runModeCombo_;
    QComboBox* cornerCombo_;
    TrustBadgeWidget* trustBadge_ = nullptr;
    
    // State
    SchematicEditor* schematicEditor_ = nullptr;
    QMap<QString, Simulation::SimulationProfile> profiles_;
    QString currentProfileId_;
    size_t currentTopologyHash_ = 0;
    AppMode currentMode_ = AppMode::BENCH_TEST;
    
    // Simulation engine
    CircuitSim sim_;
    TensorNetlist netlist_;
    QTimer* simTimer_;
    double currentTime_ = 0.0;
    double timeStep_ = 1e-5;
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
    void runSensitivity();
    StatisticalDashboard* statsDashboard_ = nullptr;
    std::vector<double> lastMonteCarloResults_;
};
