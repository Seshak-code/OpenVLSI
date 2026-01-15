#pragma once

#include <QWidget>
#include <QListWidget>
#include <QVBoxLayout>
#include <QTabWidget>
#include "visual_component.h"
#include "design_flow.h"

// ============================================================================
// COMPONENT LIBRARY
// ============================================================================
class ComponentLibrary : public QWidget {
    Q_OBJECT
public:
    explicit ComponentLibrary(QWidget* parent = nullptr);
    
    void setStage(DesignStage stage);

signals:
    void componentSelected(ComponentType type);

private:
    QTabWidget* tabs;
    QListWidget* basicList;
    QListWidget* activeList;
    QListWidget* sourcesList;
    QListWidget* systemList; // [NEW] PDN-specific parts

    void setupUi();
    void populateLists();
    void populateSystemList();
    void addComponentItem(QListWidget* list, const QString& name, const QString& iconName, ComponentType type);
};
