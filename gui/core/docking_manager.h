#pragma once

#include <QObject>
#include <QMainWindow>
#include <QDockWidget>
#include <QMap>
#include "../widgets/terminal_widget.h"
#include "../widgets/output_dock_widget.h"
#include "../widgets/waveform_widget.h"
// Forward Decls
class ProblemsPanel;
class DebugSuggestionsPanel;
class QTreeWidget;

class DockingManager : public QObject {
    Q_OBJECT
public:
    explicit DockingManager(QMainWindow* mainWindow);

    // Factory Methods
    void createCoreDocks();
    
    // Accessors
    QDockWidget* getDock(const QString& id) const;
    TerminalWidget* terminal() const { return terminalWidget_; }
    WaveformWidget* waveform() const { return waveformWidget_; } 
    ProblemsPanel* problems() const { return problemsPanel_; }
    DebugSuggestionsPanel* debugSuggestions() const { return debugPanel_; }
    QTreeWidget* results() const { return resultsTree_; }
    OutputDockWidget* output() const { return outputWidget_; }

public slots:
    void onPromoteToTab(QWidget* widget, const QString& title);
    void onReturnToDock(QWidget* widget, const QString& dockId);

signals:
    void tabPromotionRequested(QWidget* widget, const QString& title);
    void dockRestorationRequested(QWidget* widget);

private:
    // Helpers
    void setupBottomPanel(); // Verify tab grouping
    QMainWindow* mainWindow_;
    QMap<QString, QDockWidget*> docks_;
    
    // Core Widgets managed by docking system
    TerminalWidget* terminalWidget_ = nullptr;
    WaveformWidget* waveformWidget_ = nullptr;
    ProblemsPanel* problemsPanel_ = nullptr;
    DebugSuggestionsPanel* debugPanel_ = nullptr;
    QTreeWidget* resultsTree_ = nullptr;
    OutputDockWidget* outputWidget_ = nullptr;
    
    QDockWidget* createDock(const QString& id, const QString& title, QWidget* innerWidget, 
                           Qt::DockWidgetArea defaultArea = Qt::BottomDockWidgetArea);
};
