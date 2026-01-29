#pragma once

#include <QWidget>
#include <QListWidget>
#include <QVBoxLayout>
#include <QTabWidget>
#include <QLineEdit>
#include "visual_component.h"
#include "design_flow.h"
#include "gui_types.h"

// ============================================================================
// COMPONENT LIBRARY (Unified - All Components Available)
// ============================================================================
class ComponentLibrary : public QWidget {
    Q_OBJECT
public:
    explicit ComponentLibrary(QWidget* parent = nullptr);

    // Unified library - no modes
    
signals:
    void componentSelected(ComponentType type, const QString& name);

private slots:
    void onSearchTextChanged(const QString& text);

private:
    QLineEdit* searchEdit_;
    QTabWidget* tabs;
    QListWidget* basicList; // Primitives
    QListWidget* pdkList;   // Standard Cells / PDK

    void setupUi();
    void populateLists();
    void addComponentItem(QListWidget* list, const QString& name, const QString& iconName, ComponentType type);
    void filterList(QListWidget* list, const QString& pattern);
};
