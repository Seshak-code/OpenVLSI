#pragma once

#include <QMainWindow>
#include <QDockWidget>
#include <QTreeWidget>
#include <QTableWidget>
#include <QTabWidget>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QToolBar>
#include <QFileSystemModel>
#include <QTreeView>
#include "schematic_editor.h"
#include "ai_assistant.h"
#include "component_library.h"
#include "simulator_window.h"
#include "system_inspector.h"
#include <QStackedWidget>

// ============================================================================
// MAIN WINDOW
// ============================================================================
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    SchematicEditor* getSchematicEditor() const { return schematicEditor; }
    
    // Mode Management
    enum class AppMode { LAB_BENCHMARK, SYSTEM_INTEGRATION };
    AppMode getMode() const { return currentMode; }
    void setMode(AppMode mode);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    // Core Components
    QTabWidget* centralTabs;
    SchematicEditor* schematicEditor;
    SimulatorWindow* simulatorPanel;
    
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
    QTableWidget* messageTable;

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
    
    // Actions
    void newProject();
    void openProject();
    void runSimulation();
    
    // Mode Switching
    void setupModeToolbar();
    void onModeToggle();

private:
    AppMode currentMode = AppMode::SYSTEM_INTEGRATION;
    QToolBar* modeToolbar;
    QAction* toggleModeAction;

private slots:
    void updateProperties(VisualComponent* item);
    void updateWireProperties(VisualWire* wire);
    void onPropertyEdited(QTableWidgetItem* item);

private:
    VisualComponent* activeComponent = nullptr;
    VisualWire* activeWire = nullptr;
    bool isUpdating = false; // Prevent feedback loop
};
