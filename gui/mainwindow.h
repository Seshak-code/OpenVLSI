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
#include "schematic_editor.h"

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
    SchematicEditor* schematicEditor;
    
    // Docks
    QDockWidget* projectDock;
    QTreeWidget* projectTree;

    QDockWidget* propertiesDock;
    QTableWidget* propertiesTable;

    QDockWidget* messagesDock;
    QTableWidget* messageTable;

    // UI Setup Methods
    void setupMenus();
    void setupRibbon(); // Placeholder for future ribbon widget
    void setupDocks();
    void setupCentralWidget();
    
    // Helpers
    void createProjectDock();
    void createPropertiesDock();
    void createMessagesDock();
    
    // Actions
    void newProject();
    void openProject();
    void runSimulation();

private slots:
    void updateProperties(VisualComponent* item);
    void onPropertyEdited(QTableWidgetItem* item);

private:
    VisualComponent* activeComponent = nullptr;
    bool isUpdating = false; // Prevent feedback loop
};
