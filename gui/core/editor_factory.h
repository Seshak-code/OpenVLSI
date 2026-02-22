#pragma once

#include "editor_manager.h"
#include <QString>
#include <QStringList>
#include <QList>
#include <functional>

namespace acutesim::gui::core {

struct EditorRegistration {
    QString typeId;                                       // e.g. "schematic", "code", "json"
    QStringList extensions;                               // e.g. {".sch"}, {".v", ".sv"}
    QString displayName;
    QString iconName;                                     // Icon key for IconProvider
    std::function<IEditor*(const QString& path)> factory; // Factory callable
    int priority = 0;                                     // Higher wins on extension conflict
};

/**
 * @brief Extensible registry mapping file extensions → editor factories.
 *
 * Built-in registrations happen in MainWindow or main().
 * Plugins/extensions can call registerEditor() at any time.
 * createEditor() resolves by extension; ties broken by priority (desc).
 * Unknown extensions fall back to the lowest-priority registration with
 * typeId == "code".
 */
class EditorFactory {
public:
    static EditorFactory& instance();

    void registerEditor(const EditorRegistration& reg);

    // Create an editor for the given path.  typeHint overrides extension lookup.
    IEditor* createEditor(const QString& path, const QString& typeHint = "") const;

    // All extensions currently handled (for file-open filter building)
    QStringList supportedExtensions() const;

    // Icon name for a given extension (falls back to "file")
    QString iconForExtension(const QString& ext) const;

private:
    EditorFactory() = default;
    QList<EditorRegistration> registrations_;
};

} // namespace acutesim::gui::core
