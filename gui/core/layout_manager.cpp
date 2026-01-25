#include "layout_manager.h"
#include <QCoreApplication>
#include <QDebug>
#include <QRegularExpression>
#include <QDockWidget>

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
    initBuiltInPresets();
}

void LayoutManager::initBuiltInPresets() {
    // Define built-in workspace presets
    builtInPresets_ = {
        {"default", "Default", "Standard balanced layout for general use", true},
        {"analog_debug", "Analog Debugging", "Optimized for analog circuit debugging with waveforms prominent", true},
        {"rtl_synthesis", "RTL Synthesis", "Focus on HDL code and synthesis results", true},
        {"simulation", "Simulation Focus", "Maximized simulation workspace and results panels", true},
        {"minimal", "Minimal", "Maximum schematic canvas with hidden panels", true}
    };
}

void LayoutManager::saveWindowLayout(const QString& key, QMainWindow* window) {
    if (!window) return;
    settings_.beginGroup(key);
    settings_.setValue("geometry", window->saveGeometry());
    settings_.setValue("state", window->saveState());
    settings_.endGroup();
    settings_.sync();
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
    settings_.sync();
}

bool LayoutManager::restoreSplitterState(const QString& key, QSplitter* splitter) {
    if (!splitter) return false;
    QByteArray state = settings_.value(key + "_splitter").toByteArray();
    if (state.isEmpty()) return false;
    return splitter->restoreState(state);
}

QList<WorkspacePreset> LayoutManager::availableLayouts() {
    QList<WorkspacePreset> result = builtInPresets_;

    // Add user-created layouts
    settings_.beginGroup("user_layouts");
    QStringList userLayouts = settings_.childGroups();
    for (const QString& id : userLayouts) {
        WorkspacePreset preset;
        preset.id = id;
        preset.name = settings_.value(id + "/name", id).toString();
        preset.description = settings_.value(id + "/description", "User-created layout").toString();
        preset.isBuiltIn = false;
        result.append(preset);
    }
    settings_.endGroup();

    return result;
}

void LayoutManager::saveLayoutAs(const QString& name, QMainWindow* window) {
    if (!window || name.isEmpty()) return;

    // Generate ID from name
    QString id = name.toLower().replace(' ', '_').replace(QRegularExpression("[^a-z0-9_]"), "");
    if (id.isEmpty()) id = "custom_layout";

    // Check if it conflicts with built-in
    for (const auto& preset : builtInPresets_) {
        if (preset.id == id) {
            id = "user_" + id;
            break;
        }
    }

    // Save layout data
    settings_.beginGroup("user_layouts");
    settings_.beginGroup(id);
    settings_.setValue("name", name);
    settings_.setValue("description", "Custom layout: " + name);
    settings_.setValue("geometry", window->saveGeometry());
    settings_.setValue("state", window->saveState());
    settings_.endGroup();
    settings_.endGroup();
    settings_.sync();

    currentLayoutId_ = id;
    emit layoutChanged(id);

    qDebug() << "[LayoutManager] Saved layout:" << name << "as" << id;
}

bool LayoutManager::loadLayout(const QString& layoutId, QMainWindow* window) {
    if (!window || layoutId.isEmpty()) return false;

    // Check if it's a built-in preset
    bool isBuiltIn = false;
    for (const auto& preset : builtInPresets_) {
        if (preset.id == layoutId) {
            isBuiltIn = true;
            break;
        }
    }

    bool success = false;

    if (isBuiltIn) {
        // Built-in presets use the main_window key with preset suffix
        QString key = "main_window_" + layoutId;
        success = restoreWindowLayout(key, window);

        // If no saved state for this preset, apply programmatic configuration
        if (!success) {
            qDebug() << "[LayoutManager] No saved state for built-in preset:" << layoutId << "- applying programmatic config";
            applyBuiltInPreset(layoutId, window);
            success = true;
        }
    } else {
        // User layout
        settings_.beginGroup("user_layouts");
        settings_.beginGroup(layoutId);

        QByteArray geom = settings_.value("geometry").toByteArray();
        QByteArray state = settings_.value("state").toByteArray();

        settings_.endGroup();
        settings_.endGroup();

        if (!geom.isEmpty()) window->restoreGeometry(geom);
        if (!state.isEmpty()) window->restoreState(state);

        success = !geom.isEmpty() || !state.isEmpty();
    }

    if (success) {
        currentLayoutId_ = layoutId;
        emit layoutChanged(layoutId);
        qDebug() << "[LayoutManager] Loaded layout:" << layoutId;
    }

    return success;
}

void LayoutManager::applyBuiltInPreset(const QString& presetId, QMainWindow* window) {
    if (!window) return;

    // Get all dock widgets
    QList<QDockWidget*> docks = window->findChildren<QDockWidget*>();

    if (presetId == "minimal") {
        // Hide all docks for maximum canvas space
        for (auto* dock : docks) {
            dock->hide();
        }
        qDebug() << "[LayoutManager] Applied minimal preset - all docks hidden";
    }
    else if (presetId == "analog_debug") {
        // Analog debugging: Show waveform prominently, hide project explorer
        for (auto* dock : docks) {
            QString name = dock->objectName().toLower();
            if (name.contains("waveform") || name.contains("wave")) {
                dock->show();
                dock->raise();
            } else if (name.contains("project") || name.contains("explorer")) {
                dock->hide();
            } else if (name.contains("properties") || name.contains("output") || name.contains("problems")) {
                dock->show();
            }
        }
        qDebug() << "[LayoutManager] Applied analog_debug preset";
    }
    else if (presetId == "rtl_synthesis") {
        // RTL Synthesis: HDL editor prominent, terminal visible, waveform minimized
        for (auto* dock : docks) {
            QString name = dock->objectName().toLower();
            if (name.contains("waveform") || name.contains("wave")) {
                dock->hide();
            } else if (name.contains("terminal") || name.contains("output")) {
                dock->show();
            } else if (name.contains("project") || name.contains("explorer")) {
                dock->show();
            }
        }
        qDebug() << "[LayoutManager] Applied rtl_synthesis preset";
    }
    else if (presetId == "simulation") {
        // Simulation focus: All bottom panels visible, waveform prominent
        for (auto* dock : docks) {
            QString name = dock->objectName().toLower();
            if (name.contains("waveform") || name.contains("wave") ||
                name.contains("output") || name.contains("result") ||
                name.contains("problems") || name.contains("terminal")) {
                dock->show();
                if (name.contains("waveform") || name.contains("wave")) {
                    dock->raise();
                }
            }
        }
        qDebug() << "[LayoutManager] Applied simulation preset";
    }
    else if (presetId == "default") {
        // Default: Show all standard docks in balanced configuration
        for (auto* dock : docks) {
            QString name = dock->objectName().toLower();
            // Show core docks, hide specialized ones
            if (name.contains("worker") || name.contains("debug_suggestion")) {
                dock->hide();
            } else {
                dock->show();
            }
        }
        qDebug() << "[LayoutManager] Applied default preset";
    }
}

bool LayoutManager::deleteLayout(const QString& layoutId) {
    // Can't delete built-in presets
    for (const auto& preset : builtInPresets_) {
        if (preset.id == layoutId) {
            qDebug() << "[LayoutManager] Cannot delete built-in preset:" << layoutId;
            return false;
        }
    }

    settings_.beginGroup("user_layouts");
    settings_.remove(layoutId);
    settings_.endGroup();
    settings_.sync();

    qDebug() << "[LayoutManager] Deleted layout:" << layoutId;
    return true;
}

void LayoutManager::resetToFactory() {
    settings_.clear();
    settings_.sync();
    currentLayoutId_ = "default";
    emit layoutChanged("default");
}

QByteArray LayoutManager::getSavedState(const QString& key) const {
    QString groupKey = key + "/state";
    return settings_.value(groupKey).toByteArray();
}

} // namespace core
} // namespace gui
} // namespace acutesim
