#pragma once

#include <QObject>
#include <QSettings>
#include <QKeySequence>
#include <QAction>
#include <QShortcut>
#include <QMap>
#include <QList>
#include <functional>

namespace acutesim {
namespace gui {
namespace core {

/**
 * KeyBinding - Represents a single keyboard shortcut binding
 */
struct KeyBinding {
    QString id;              // Unique identifier (e.g., "file.save")
    QString label;           // Display name (e.g., "Save")
    QString category;        // Category for grouping (e.g., "File")
    QKeySequence defaultKey; // Default shortcut
    QKeySequence currentKey; // Current (possibly user-customized) shortcut
    QString context;         // Context when active (e.g., "editor", "global")
    bool isCustomized;       // Whether user has customized this binding
};

/**
 * KeybindingManager - VS Code-style centralized keybinding management
 *
 * Features:
 * - Centralized shortcut registration and management
 * - User customization with persistence
 * - Conflict detection
 * - Context-aware shortcuts
 * - Search/filter capabilities
 */
class KeybindingManager : public QObject {
    Q_OBJECT

public:
    static KeybindingManager& instance();

    // Registration
    void registerAction(const QString& id, const QString& label,
                       const QString& category, const QKeySequence& defaultKey,
                       QAction* action, const QString& context = "global");

    void registerCallback(const QString& id, const QString& label,
                         const QString& category, const QKeySequence& defaultKey,
                         std::function<void()> callback, QWidget* parent,
                         const QString& context = "global");

    // Customization
    bool setKeybinding(const QString& id, const QKeySequence& newKey);
    bool resetToDefault(const QString& id);
    void resetAllToDefaults();

    // Queries
    QList<KeyBinding> getAllBindings() const;
    QList<KeyBinding> getBindingsForCategory(const QString& category) const;
    QList<QString> getCategories() const;
    KeyBinding getBinding(const QString& id) const;

    // Conflict detection
    QString findConflict(const QKeySequence& key, const QString& context = "global") const;
    bool hasConflict(const QKeySequence& key, const QString& context = "global") const;

    // Search
    QList<KeyBinding> search(const QString& query) const;

    // Import/Export
    QString exportToJson() const;
    bool importFromJson(const QString& json);

signals:
    void keybindingChanged(const QString& id, const QKeySequence& oldKey, const QKeySequence& newKey);
    void keybindingsReset();

private:
    KeybindingManager();
    ~KeybindingManager() override = default;
    KeybindingManager(const KeybindingManager&) = delete;
    KeybindingManager& operator=(const KeybindingManager&) = delete;

    void loadCustomizations();
    void saveCustomizations();
    void applyBinding(const QString& id);

    QSettings settings_;
    QMap<QString, KeyBinding> bindings_;
    QMap<QString, QAction*> actions_;
    QMap<QString, QShortcut*> shortcuts_;
    QMap<QString, std::function<void()>> callbacks_;
};

} // namespace core
} // namespace gui
} // namespace acutesim
