#pragma once
#include "engine_api/simulation_context.h"
#include "gui/core/sim_auto_detector.h"
#include <QObject>
#include <QTabWidget>
#include <QMap>
#include <QString>
#include <QIcon>
#include <QUuid>

namespace acutesim::gui::core {

// Defines what an editor instance can do
struct EditorCapabilities {
    bool canSimulate = false;       // e.g. Schematics
    bool canSynthesize = false;     // e.g. Verilog/SystemVerilog
    bool canRunScript = false;      // e.g. Python/TCL
    bool isReadOnly = false;        // e.g. Reports/Logs
};

// Base interface for all editor widgets (Schematic, Code, etc.)
class IEditor : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;
    virtual ~IEditor() = default;

    virtual QString filePath() const = 0;
    virtual QString title() const = 0;
    virtual bool isDirty() const = 0;
    virtual bool save() = 0;  // writes the document to filePath(); false: nothing was written
    virtual EditorCapabilities capabilities() const = 0;
    
    // Unique ID for tracking this editor instance
    QUuid id() const { return id_; }

signals:
    void contentChanged();     // Triggers dirty state check
    void titleChanged(const QString& newTitle);

protected:
    QUuid id_ = QUuid::createUuid();
};

class EditorManager : public QObject {
    Q_OBJECT
public:
    explicit EditorManager(QTabWidget* tabs, QObject* parent = nullptr);

    // Core Actions
    void openFile(const QString& path, const QString& typeHint = "");
    void closeEditor(int index);
    void closeAll();
    
    // Editor Access
    IEditor* currentEditor() const;
    IEditor* getEditor(int index) const;
    acutesim::orchestration::SimulationContext currentContext() const { return currentContext_; }

signals:
    void activeEditorChanged(IEditor* editor);
    void contextChanged(const acutesim::orchestration::SimulationContext& context);
    void editorsChanged(); // Count changed

private slots:
    void onTabChanged(int index);
    void onTabCloseRequested(int index);
    void onEditorContentChanged();

private:
    QTabWidget* tabWidget_;
    QMap<QWidget*, IEditor*> editorMap_; // Maps tab widget back to interface
    ::gui::core::SimAutoDetector  detector_;
    acutesim::orchestration::SimulationContext currentContext_;
    
    // Helpers
    IEditor* createEditorForFile(const QString& path, const QString& typeHint);
    void updateTabTitle(IEditor* editor);
    void updateContext(const QString& filePath, IEditor* editor);
    void doCloseEditor(int index, QWidget* widget);
};

} // namespace acutesim::gui::core
