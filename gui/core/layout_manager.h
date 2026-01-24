#pragma once

#include <QObject>
#include <QSettings>
#include <QMainWindow>
#include <QSplitter>
#include <QByteArray>
#include <QStringList>

namespace acutesim {
namespace gui {
namespace core {

/**
 * WorkspacePreset - Predefined workspace layout configurations
 */
struct WorkspacePreset {
    QString id;
    QString name;
    QString description;
    bool isBuiltIn;  // true = preset, false = user-created
};

/**
 * LayoutManager - VS Code-style workspace layout persistence
 *
 * Supports:
 * - Window geometry/dock state persistence
 * - Named workspace layouts (presets)
 * - Custom user-defined layouts
 */
class LayoutManager : public QObject {
    Q_OBJECT
public:
    static LayoutManager& instance();

    // Window State Persistence
    void saveWindowLayout(const QString& key, QMainWindow* window);
    bool restoreWindowLayout(const QString& key, QMainWindow* window);

    // Splitter Persistence
    void saveSplitterState(const QString& key, QSplitter* splitter);
    bool restoreSplitterState(const QString& key, QSplitter* splitter);

    // === Workspace Layout Management (Phase 11) ===

    // Get list of available layouts (built-in + user)
    QList<WorkspacePreset> availableLayouts();

    // Save current layout as a named preset
    void saveLayoutAs(const QString& name, QMainWindow* window);

    // Load a named layout
    bool loadLayout(const QString& layoutId, QMainWindow* window);

    // Delete a user-created layout
    bool deleteLayout(const QString& layoutId);

    // Get current active layout ID
    QString currentLayout() const { return currentLayoutId_; }

    // Global Reset
    void resetToFactory();

    // Accessors for raw data (for testing)
    QByteArray getSavedState(const QString& key) const;

signals:
    void layoutChanged(const QString& layoutId);

private:
    LayoutManager();
    ~LayoutManager() override = default;
    LayoutManager(const LayoutManager&) = delete;
    LayoutManager& operator=(const LayoutManager&) = delete;

    void initBuiltInPresets();

    QSettings settings_;
    QList<WorkspacePreset> builtInPresets_;
    QString currentLayoutId_ = "default";
};

} // namespace core
} // namespace gui
} // namespace acutesim
