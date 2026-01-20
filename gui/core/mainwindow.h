#pragma once

#include <QMainWindow>
#include <QDockWidget>
#include <QTreeWidget>
#include <QTableWidget>
#include <QTabWidget>
#include <QDialog>
#include <QJsonObject>
#include <QJsonArray>
#include <QPlainTextEdit>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QToolBar>
#include <QFileSystemModel>
#include <QTreeView>
#include "schematic_editor.h"
#include "ai_assistant.h"
#include "component_library.h"
#include "simulation_workspace.h"
#include "system_inspector.h"
#include "design_flow.h"
#include "hdl_editor.h"  // Phase 14.1
#include <QStackedWidget>
#include <QSplitter>
#include "gui_types.h"
#include "welcome_widget.h"
#include "problems_panel.h"

// ============================================================================
// MAIN WINDOW
// ============================================================================
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    SchematicEditor* getSchematicEditor() const { return schematicEditor; }
    
    // Mode Management
    AppMode getMode() const; 
    void setMode(AppMode mode);

    // AI/API Access
    void openSchematicTab(const QString& title, const QJsonObject& content);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    // Design Flow Management
    DesignFlowManager* flowManager;
    QTabWidget* stageTabs;           // Top-level stage tabs (RTL, Synthesis, Physical, Verification)
    QTabWidget* rtlPageTabs;         // Sub-tabs within RTL stage (multi-page schematics)
    
    // Core Components
    SchematicEditor* schematicEditor; // Active schematic editor (points to current RTL page)
    HDLEditor* hdlEditor; // Phase 14: Code Editor
    SimulationWorkspace* simulationWorkspace; // Phase 1.7: VS Code-style simulation
    WelcomeWidget* welcomeWidget;
    
    // Docks
    QDockWidget* projectDock;
    QTreeView* projectView;
    QFileSystemModel* fileModel;

    QDockWidget* libraryDock;
    ComponentLibrary* libraryPanel;

    QDockWidget* propertiesDock;
    QStackedWidget* propertiesStack; // [NEW] Switcher
    QTableWidget* propertiesTable;   // For Component Mode
    SystemInspector* systemInspector; // For System Mode
    
    QDockWidget* aiDock;
    AiAssistantPanel* aiPanel;

    QDockWidget* messagesDock;
    ProblemsPanel* problemsPanel;
    
    // Bottom dock panels for debug output
    QDockWidget* outputDock_;
    QPlainTextEdit* outputLog_;
    QDockWidget* streamingDock_;
    QPlainTextEdit* streamingLog_;
    QDockWidget* resultsDock_;
    QTreeWidget* resultsTree_;      // [NEW] Tree view for results
    QDockWidget* waveformDock_;
    QPlainTextEdit* waveformLog_;   // [NEW] Text view for waveform data dump (placeholder for plot)

private slots:
    void onSimulationResults(const QJsonObject& results);

    // UI Setup Methods
    void setupMenus();
    void setupDocks();
    void setupCentralWidget();
    
    // Helpers
    void createProjectDock();
    void createLibraryDock();
    void createPropertiesDock();
    void createAiDock();
    void createMessagesDock();
    void createOutputDock();
    void createStreamingDock();
    void createResultsDock();
    void createWaveformDock();
    
    // Actions
    void newProject();
    void openProject();
    void runSimulation();
    
    // Stage Switching (replaces old Mode Switching)
    void setupStageToolbar();
    void onStageChanged(DesignStage stage);
    void addRtlPage();
    void createSchematicFromWelcome(AppMode mode);
    void onRtlTabChanged(int index);
    QString modeLabel(AppMode mode);

private:
    AppMode currentMode = AppMode::SYSTEM_INTEGRATION;
    QToolBar* modeToolbar;
    QAction* toggleModeAction;
    
    void onModeToggle();
    void syncSchematicToVerilog();
    void syncVerilogToSchematic();

private slots:
    void updateProperties(VisualComponent* item);
    void updateWireProperties(VisualWire* wire);
    void onPropertyEdited(QTableWidgetItem* item);

private:
    VisualComponent* activeComponent = nullptr;
    VisualWire* activeWire = nullptr;
    bool isUpdating = false; // Prevent feedback loop
};
