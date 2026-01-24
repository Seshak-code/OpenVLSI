#include "layout_manager.h"
#include <QCoreApplication>

namespace acutesim {
namespace gui {
namespace core {

LayoutManager& LayoutManager::instance() {
    static LayoutManager instance;
    return instance;
}

LayoutManager::LayoutManager() 
    : settings_(QSettings::IniFormat, QSettings::UserScope, "AcuteSim", "Layouts") 
{
    // Ensure portable file location or company name consistency
}

void LayoutManager::saveWindowLayout(const QString& key, QMainWindow* window) {
    if (!window) return;
    settings_.beginGroup(key);
    settings_.setValue("geometry", window->saveGeometry());
    settings_.setValue("state", window->saveState());
    settings_.endGroup();
}

bool LayoutManager::restoreWindowLayout(const QString& key, QMainWindow* window) {
    if (!window) return false;
    settings_.beginGroup(key);
    bool g = false, s = false;
    
    QByteArray geom = settings_.value("geometry").toByteArray();
    if (!geom.isEmpty()) g = window->restoreGeometry(geom);
    
    QByteArray state = settings_.value("state").toByteArray();
    if (!state.isEmpty()) s = window->restoreState(state);
    
    settings_.endGroup();
    return g || s;
}

void LayoutManager::saveSplitterState(const QString& key, QSplitter* splitter) {
    if (!splitter) return;
    settings_.setValue(key + "_splitter", splitter->saveState());
}

bool LayoutManager::restoreSplitterState(const QString& key, QSplitter* splitter) {
    if (!splitter) return false;
    QByteArray state = settings_.value(key + "_splitter").toByteArray();
    if (state.isEmpty()) return false;
    return splitter->restoreState(state);
}

void LayoutManager::resetToFactory() {
    settings_.clear();
    settings_.sync(); // Ensure flush to disk
}

QByteArray LayoutManager::getSavedState(const QString& key) const {
    // Helper to peek directly at setting (useful for tests)
    // We recreate the group logic manually since verifying raw storage
    QString groupKey = key + "/state";
    return settings_.value(groupKey).toByteArray();
}

} // namespace core
} // namespace gui
} // namespace acutesim
