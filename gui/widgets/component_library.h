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

    void setStage(DesignStage stage);

    // DEPRECATED: Mode filtering removed - all tabs now always visible
    // Kept for API compatibility but is now a no-op
    void setMode(AppMode mode);

signals:
    void componentSelected(ComponentType type);

private slots:
    void onSearchTextChanged(const QString& text);

private:
    QLineEdit* searchEdit_;
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
    void filterList(QListWidget* list, const QString& pattern);
};
