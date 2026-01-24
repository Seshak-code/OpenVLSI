#include "docking_manager.h"
#include "docking_manager.h"
#include "../widgets/output_dock_widget.h"
#include "../widgets/waveform_widget.h"
#include <QLabel> // For placeholder content if needed

DockingManager::DockingManager(QMainWindow* mainWindow) 
    : QObject(mainWindow), mainWindow_(mainWindow) 
{
    // NASA Principle: Fail early if parent is null
    Q_ASSERT(mainWindow_);
}

QDockWidget* DockingManager::createDock(const QString& id, const QString& title, QWidget* innerWidget, Qt::DockWidgetArea defaultArea) {
    QDockWidget* dock = new QDockWidget(title, mainWindow_);
    dock->setObjectName(id);
    // User Requirement: Allow docking anywhere
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);
    dock->setWidget(innerWidget);
    
    mainWindow_->addDockWidget(defaultArea, dock);
    docks_[id] = dock;
    return dock;
}

void DockingManager::createCoreDocks() {
    // 1. Terminal (The Command Center)
    terminalWidget_ = new TerminalWidget(mainWindow_);
    createDock("terminal", "Terminal", terminalWidget_, Qt::BottomDockWidgetArea);

    // 2. Output Panels
    // OutputDockWidget handles its own scrolling/clear logic
    createDock("output", "Output", new OutputDockWidget("Output", mainWindow_), Qt::BottomDockWidgetArea);
    createDock("problems", "Problems", new OutputDockWidget("Problems", mainWindow_), Qt::BottomDockWidgetArea); // Temp using OutputDockWidget for problems placeholder
    
    // 3. Waveforms (Phase 11.3)
    waveformWidget_ = new WaveformWidget(mainWindow_);
    createDock("waveforms", "Waveforms", waveformWidget_, Qt::BottomDockWidgetArea);
    
    // Connect Signals
    connect(waveformWidget_, &WaveformWidget::promoteToTabRequested, this, [this](){
        onPromoteToTab(waveformWidget_, "Waveforms");
    });
    connect(waveformWidget_, &WaveformWidget::returnToDockRequested, this, [this](){
        onReturnToDock(waveformWidget_, "waveforms");
    });
    
    setupBottomPanel();
}

void DockingManager::setupBottomPanel() {
    // Terminal | Output | Problems | Waveforms
    QDockWidget* term = docks_.value("terminal");
    QDockWidget* out = docks_.value("output");
    QDockWidget* prob = docks_.value("problems");
    QDockWidget* wave = docks_.value("waveforms");
    
    if (term && out && prob && wave) {
        mainWindow_->tabifyDockWidget(term, out);
        mainWindow_->tabifyDockWidget(out, prob);
        mainWindow_->tabifyDockWidget(prob, wave);
        // Default to Problems or Terminal? Terminal for power users.
        term->raise();
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
