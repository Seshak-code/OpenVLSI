#pragma once

#include <QWidget>
#include <QListWidget>
#include <QVBoxLayout>
#include <QTabWidget>
#include "visual_component.h"
#include "design_flow.h"
#include "gui_types.h"

// ============================================================================
// COMPONENT LIBRARY
// ============================================================================
class ComponentLibrary : public QWidget {
    Q_OBJECT
public:
    explicit ComponentLibrary(QWidget* parent = nullptr);
    
    void setStage(DesignStage stage);
    
    // Mode-based filtering (Lab vs System)
    void setMode(AppMode mode);

signals:
    void componentSelected(ComponentType type);

private:
    QTabWidget* tabs;
    QListWidget* basicList;
    QListWidget* activeList;
    QListWidget* sourcesList;
    QListWidget* systemList; // [NEW] PDN-specific parts
    QListWidget* icsList;    // [NEW] IC Packages (System mode)

    void setupUi();
    void populateLists();
    void populateSystemList();
    void populateICsList();
    void addComponentItem(QListWidget* list, const QString& name, const QString& iconName, ComponentType type);
};
