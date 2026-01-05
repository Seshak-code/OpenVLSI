#pragma once

#include <QWidget>
#include <QListWidget>
#include <QVBoxLayout>
#include <QTabWidget>
#include "visual_component.h"

// ============================================================================
// COMPONENT LIBRARY
// ============================================================================
class ComponentLibrary : public QWidget {
    Q_OBJECT
public:
    explicit ComponentLibrary(QWidget* parent = nullptr);

signals:
    void componentSelected(ComponentType type);

private:
    QTabWidget* tabs;
    QListWidget* basicList;
    QListWidget* activeList;
    QListWidget* sourcesList;

    void setupUi();
    void populateLists();
    void addComponentItem(QListWidget* list, const QString& name, const QString& iconName, ComponentType type);
};
