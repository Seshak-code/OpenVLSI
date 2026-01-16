#pragma once

#include <QWidget>
#include <QListWidget>
#include <QVBoxLayout>
#include <QTabWidget>
#include "visual_component.h"
#include "design_flow.h"
#include "gui_types.h"

// ============================================================================
// COMPONENT LIBRARY (Three-Mode Architecture)
// ============================================================================
class ComponentLibrary : public QWidget {
    Q_OBJECT
public:
    explicit ComponentLibrary(QWidget* parent = nullptr);
    
    void setStage(DesignStage stage);
    
    // Three-mode filtering: Bench / Block / System
    void setMode(AppMode mode);

signals:
    void componentSelected(ComponentType type);

private:
    QTabWidget* tabs;
    QListWidget* basicList;
    QListWidget* activeList;
    QListWidget* sourcesList;       // Bench: ideal + dependent sources
    QListWidget* interfacesList;    // Block: PowerPort pins
    QListWidget* systemList;        // System: PDN, RailDrivers
    QListWidget* icsList;           // System: IC Packages

    void setupUi();
    void populateLists();
    void addComponentItem(QListWidget* list, const QString& name, const QString& iconName, ComponentType type);
};
