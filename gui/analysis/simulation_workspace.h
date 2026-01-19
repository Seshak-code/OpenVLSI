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
#include "simulation_explorer.h"
#include "simulation_profile.h"
#include "../editors/schematic_editor.h"
#include "circuitsim.h"

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
    
signals:
    void simulationStarted(const QString& profileId);
    void simulationCompleted(const Simulation::SimulationRun& run);
    void navigateToNode(int nodeId);
    
public slots:
    void runCurrent();
    void runQuickDC();
    void stopSimulation();
    
private slots:
    void onProfileSelected(const QString& profileId);
    void onRunRequested(const QString& profileId);
    void stepSimulation();
    void appendLog(const QString& msg);
    void appendStreamLog(const QString& msg);
    
private:
    // Layout
    QSplitter* mainSplitter_;
    QSplitter* rightSplitter_;
    
    // Left panel
    SimulationExplorer* explorer_;
    
    // Center panel
    QWidget* profileEditor_;
    QLabel* profileNameLabel_;
    QComboBox* analysisCombo_;
    QWidget* parametersWidget_;
    
    // Bottom panel
    QTabWidget* outputTabs_;
    QPlainTextEdit* outputLog_;
    QPlainTextEdit* streamingLog_;
    QWidget* resultsPanel_;
    QWidget* waveformPanel_;
    
    // Toolbar
    QWidget* toolbar_;
    QPushButton* runBtn_;
    QPushButton* stopBtn_;
    QComboBox* profileCombo_;
    
    // State
    SchematicEditor* schematicEditor_ = nullptr;
    QMap<QString, Simulation::SimulationProfile> profiles_;
    QString currentProfileId_;
    size_t currentTopologyHash_ = 0;
    
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
    void setupOutputTabs();
    void updateProfileEditor();
    void updateParametersForAnalysis(Simulation::AnalysisType type);
    void finishSimulation();
    bool canRun() const;
    
    // Parameter widget helpers
    QWidget* createLineParam(const QString& defaultVal);
    QWidget* createComboParam(const QStringList& options, const QString& defaultVal);
};
