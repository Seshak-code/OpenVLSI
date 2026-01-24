#include "docking_manager.h"
#include "../widgets/output_dock_widget.h"
#include "../widgets/waveform_widget.h"
#include "../widgets/problems_panel.h"
#include "../widgets/debug_suggestions_panel.h"
#include <QLabel>
#include <QSplitter>
#include <QTreeWidget>

DockingManager::DockingManager(QMainWindow* mainWindow) 
    : QObject(mainWindow), mainWindow_(mainWindow) 
{
    Q_ASSERT(mainWindow_);
}

QDockWidget* DockingManager::createDock(const QString& id, const QString& title, QWidget* innerWidget, Qt::DockWidgetArea defaultArea) {
    QDockWidget* dock = new QDockWidget(title, mainWindow_);
    dock->setObjectName(id);
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);
    dock->setWidget(innerWidget);
    
    mainWindow_->addDockWidget(defaultArea, dock);
    docks_[id] = dock;
    return dock;
}

void DockingManager::createCoreDocks() {
    // === 1. Terminal (Command Center) ===
    qDebug() << "[DM] Creating Terminal...";
    terminalWidget_ = new TerminalWidget(mainWindow_);
    createDock("terminal", "Terminal", terminalWidget_, Qt::BottomDockWidgetArea);

    // === 2. Analysis Panel (Problems | AI Debug Hints) - Side by Side ===
    qDebug() << "[DM] Creating Analysis Panel...";
    problemsPanel_ = new ProblemsPanel(mainWindow_);
    debugPanel_ = new DebugSuggestionsPanel(mainWindow_);
    
    QSplitter* analysisSplit = new QSplitter(Qt::Horizontal, mainWindow_);
    analysisSplit->addWidget(problemsPanel_);
    analysisSplit->addWidget(debugPanel_);
    analysisSplit->setSizes({300, 300}); // Equal split
    createDock("analysis", "Analysis", analysisSplit, Qt::BottomDockWidgetArea);

    // === 3. Logs Panel (Results | Output) - Side by Side ===
    qDebug() << "[DM] Creating Logs Panel...";
    resultsTree_ = new QTreeWidget(mainWindow_);
    resultsTree_->setHeaderLabels({"Node", "Value"});
    resultsTree_->setColumnWidth(0, 150);
    resultsTree_->setStyleSheet(
        "QTreeWidget { background: #1e1e1e; color: #ccc; border: none; } "
        "QHeaderView::section { background: #2d2d2d; color: #888; border: none; padding: 4px; }");
    
    outputWidget_ = new OutputDockWidget("Output", mainWindow_);
    
    QSplitter* logsSplit = new QSplitter(Qt::Horizontal, mainWindow_);
    logsSplit->addWidget(resultsTree_);
    logsSplit->addWidget(outputWidget_);
    logsSplit->setSizes({300, 300});
    createDock("logs", "Logs", logsSplit, Qt::BottomDockWidgetArea);
    
    // === 4. Waveforms ===
    qDebug() << "[DM] Creating Waveforms...";
    waveformWidget_ = new WaveformWidget(mainWindow_);
    createDock("waveforms", "Waveforms", waveformWidget_, Qt::BottomDockWidgetArea);
    
    // Connect Waveform Signals
    connect(waveformWidget_, &WaveformWidget::promoteToTabRequested, this, [this](){
        onPromoteToTab(waveformWidget_, "Waveforms");
    });
    connect(waveformWidget_, &WaveformWidget::returnToDockRequested, this, [this](){
        onReturnToDock(waveformWidget_, "waveforms");
    });
    
    qDebug() << "[DM] Setting up Bottom Panel...";
    setupBottomPanel();
}

void DockingManager::setupBottomPanel() {
    // Tabify: Terminal | Logs | Analysis | Waveforms
    QDockWidget* term = docks_.value("terminal");
    QDockWidget* logs = docks_.value("logs");
    QDockWidget* analysis = docks_.value("analysis");
    QDockWidget* wave = docks_.value("waveforms");
    
    if (term && logs && analysis && wave) {
        mainWindow_->tabifyDockWidget(term, logs);
        mainWindow_->tabifyDockWidget(logs, analysis);
        mainWindow_->tabifyDockWidget(analysis, wave);
        term->raise(); // Default to Terminal
    }
}

QDockWidget* DockingManager::getDock(const QString& id) const {
    return docks_.value(id, nullptr);
}

void DockingManager::onPromoteToTab(QWidget* widget, const QString& title) {
    // 1. Find dock containing this widget
    QDockWidget* dock = nullptr;
    for (auto d : docks_) {
        // Check exact match or if it's the current widget of the dock
        if (d->widget() == widget) {
            dock = d;
            break;
        }
    }
    
    if (!dock) return;
    
    // 2. Hide Dock (don't delete, we reuse it)
    dock->setVisible(false);
    dock->setWidget(nullptr); // Detach ownership
    
    // 3. Add to Central Tabs (via Signal)
    emit tabPromotionRequested(widget, title);
    
    // 4. Update Widget State
    if (auto wf = qobject_cast<WaveformWidget*>(widget)) {
        wf->setFloatingMode(true);
    }
}

void DockingManager::onReturnToDock(QWidget* widget, const QString& dockId) {
    // 1. Remove from Central Tabs (via Signal)
    emit dockRestorationRequested(widget);
    
    // 2. Find target dock
    QDockWidget* dock = docks_.value(dockId);
    if (!dock) return;
    
    // 3. Re-Dock
    dock->setWidget(widget);
    dock->setVisible(true);
    dock->raise();
    
    // 4. Update Widget State
    if (auto wf = qobject_cast<WaveformWidget*>(widget)) {
        wf->setFloatingMode(false);
    }
}
