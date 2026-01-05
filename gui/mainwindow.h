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

// ============================================================================
// MAIN WINDOW
// ============================================================================
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

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
    QTableWidget* propertiesTable;
    
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

private slots:
    void updateProperties(VisualComponent* item);
    void updateWireProperties(VisualWire* wire);
    void onPropertyEdited(QTableWidgetItem* item);

private:
    VisualComponent* activeComponent = nullptr;
    VisualWire* activeWire = nullptr;
    bool isUpdating = false; // Prevent feedback loop
};
