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
#include "schematic/schematic_editor.h"
#include "../panels/ai_assistant_panel.h"
#include "../widgets/component_library.h"
#include "../analysis/simulation_workspace.h"
#include "../analysis/system_inspector.h"
#include "../widgets/design_flow.h"
#include "editor_manager.h" // Phase: GUI Overhaul
#include <QStackedWidget>
#include <QSplitter>
#include <QPointer>
#include "gui_types.h"
#include "welcome_widget.h"
#include "problems_panel.h"
#include "../orchestration/worker_status_panel.h"
#include "../orchestration/rtl_pipeline_panel.h"
#include "../widgets/debug_suggestions_panel.h"
#include "../widgets/command_palette.h"
#include "../widgets/engine_log_panel.h"
#include "../widgets/engine_console.h"

class WelcomeWidget;
class DockingManager;
namespace acutesim { namespace gui { namespace core { class ActivityBar; } } }
namespace acutesim { namespace gui { namespace widgets { class ProjectExplorer; } } }
namespace acutesim { namespace gui { namespace widgets { class SearchDock; } } }

// ============================================================================
// MAIN WINDOW
// ============================================================================
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    SchematicEditor* getSchematicEditor() const { return schematicEditor; }

    // Mode management removed - unified component library

    // AI/API Access
    void openSchematicTab(const QString& title, const QJsonObject& content);

    // Phase 11.3: Central Tab Management
    void addCentralTab(QWidget* widget, const QString& title);
    void removeCentralTab(QWidget* widget);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    // Design Flow Management
    DesignFlowManager* flowManager;
    QTabWidget* stageTabs;           // Top-level stage tabs (RTL, Synthesis, Physical, Verification)
    QTabWidget* editorTabs;          // [NEW] Unified Code/Schematic Tabs
    
    // Core Components
    acutesim::gui::core::EditorManager* editorManager; // [NEW] Central Editor Controller
    SchematicEditor* schematicEditor; // Active schematic editor (points to current RTL page)
    SimulationWorkspace* simulationWorkspace; // Phase 1.7: VS Code-style simulation
    
    // Actions
    QAction* viewQuickPreviewAction; // Phase 1.7.7 Toggle
    QAction* hdlToggleAction;        // Phase 7: HDL Editor visibility
    QAction* viewDCAnnotationsAction = nullptr; // Phase 2: DC annotation toggle
    WelcomeWidget* welcomeWidget;
    
    // Docks
    QDockWidget* projectDock;
    acutesim::gui::widgets::ProjectExplorer* projectExplorer_;

    QDockWidget* libraryDock;
    ComponentLibrary* libraryPanel;

    QDockWidget* propertiesDock;
    QStackedWidget* propertiesStack; // [NEW] Switcher
    QTableWidget* propertiesTable;   // For Component Mode
    SystemInspector* systemInspector; // For System Mode
    
    QDockWidget* aiDock;
    acutesim::gui::panels::AiAssistantPanel* aiPanel;

    QDockWidget* messagesDock;
    ProblemsPanel* problemsPanel;

    QDockWidget* debugSuggestionsDock;
    acutesim::gui::widgets::DebugSuggestionsPanel* debugSuggestionsPanel;
    
    // Bottom dock panels (managed by DockingManager)
    QDockWidget* outputDock_;       // Points to DockingManager "logs" dock
    QDockWidget* waveformDock_;     // Points to DockingManager "waveforms" dock
    QTreeWidget* resultsTree_;      // Points to DockingManager results widget

    // Phase 12.3: Worker Orchestration
    QDockWidget* workerDock;
    acutesim::gui::orchestration::WorkerStatusPanel* workerPanel;

    // RTL -> GDS Pipeline Panel
    acutesim::gui::orchestration::RtlPipelinePanel* rtlPipelinePanel = nullptr;

    // Engine Log Panel
    QDockWidget* engineLogDock_ = nullptr;
    acutesim::gui::widgets::EngineLogPanel* engineLogPanel_ = nullptr;

    // Engine Console
    QDockWidget* consoleDock_ = nullptr;
    acutesim::gui::widgets::EngineConsole* engineConsole_ = nullptr;

    // Phase 11.2: Docking Manager
    DockingManager* dockingManager;

    // Phase 1.7: Simulation Dock (VS Code-style sidebar)
    QDockWidget* simulationDock_;

    // Search Dock
    QDockWidget* searchDock_;
    acutesim::gui::widgets::SearchDock* searchPanel_;


private slots:
    void onSimulationResults(const QJsonObject& results);
    void onAiSimulationRequested(const QString& analysisType);

    // UI Setup Methods
    void setupMenus();
    void setupDocks();
    void setupCentralWidget();
    
    // Helpers
    void createProjectDock();
    void populateProjectTree();
    void createLibraryDock();
    void createPropertiesDock();
    void createAiDock();
    void createMessagesDock();
    void createWorkerDock();
    void createDebugSuggestionsDock();
    void createSimulationDock();
    void createSearchDock();

    // File Operations
    void openFileFromExplorer(const QString& category, const QString& fileName, const QString& filePath);
    // detectModeFromSchematic() removed - unified component library

    // Actions
    void newProject();
    void openProject();
    void runSimulation();
    
    // Stage Switching
    void setupStageToolbar();
    void onStageChanged(DesignStage stage);

    // modeLabel() removed - unified component library

private:
    // currentMode removed - unified component library
    QToolBar* modeToolbar;
    QAction* toggleModeAction;
    
    // VS Code Style
    acutesim::gui::core::ActivityBar* activityBar_;
    acutesim::gui::widgets::CommandPalette* commandPalette_;
    void setupActivityBar();
    void setupCommandPalette();
    void setupKeybindings();
    void showCommandPalette();
    void showKeybindingsDialog();
    void applyModernTabStyle(QTabWidget* tabWidget);

    // Phase 11.2: Layout Management
    QMenu* layoutMenu_ = nullptr;
    void updateLayoutMenu();
    void resetLayout();

private slots:
    void updateProperties(VisualComponent* item);
    void updateWireProperties(VisualWire* wire);
    void onPropertyEdited(QTableWidgetItem* item);

private:
    QPointer<VisualComponent> activeComponent;
    QPointer<VisualWire> activeWire;
    bool isUpdating = false; // Prevent feedback loop
};
