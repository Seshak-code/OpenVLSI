#include "editor_factory.h"
#include <QFileInfo>
#include <algorithm>

namespace acutesim::gui::core {

EditorFactory& EditorFactory::instance() {
    static EditorFactory inst;
    return inst;
}

void EditorFactory::registerEditor(const EditorRegistration& reg) {
    registrations_.append(reg);
    // Keep sorted descending by priority for stable iteration
    std::stable_sort(registrations_.begin(), registrations_.end(),
        [](const EditorRegistration& a, const EditorRegistration& b) {
            return a.priority > b.priority;
        });
}

IEditor* EditorFactory::createEditor(const QString& path, const QString& typeHint) const {
    QFileInfo fi(path);
    QString ext = "." + fi.suffix().toLower();

    // 1. Type-hint exact match (typeId)
    if (!typeHint.isEmpty()) {
        for (const auto& reg : registrations_) {
            if (reg.typeId == typeHint && reg.factory) {
                return reg.factory(path);
            }
        }
    }

    // 2. Extension match (highest priority first due to sorted order)
    for (const auto& reg : registrations_) {
        if (reg.extensions.contains(ext) && reg.factory) {
            return reg.factory(path);
        }
    }

    // 3. Fallback: lowest-priority "code" editor
    for (auto it = registrations_.rbegin(); it != registrations_.rend(); ++it) {
        if (it->typeId == "code" && it->factory) {
            return it->factory(path);
        }
    }

    return nullptr;
}

QStringList EditorFactory::supportedExtensions() const {
    QStringList all;
    for (const auto& reg : registrations_) {
        for (const auto& ext : reg.extensions) {
            if (!all.contains(ext)) all << ext;
        }
    }
    return all;
}

QString EditorFactory::iconForExtension(const QString& ext) const {
    QString dotExt = ext.startsWith('.') ? ext : "." + ext;
    for (const auto& reg : registrations_) {
        if (reg.extensions.contains(dotExt)) {
            return reg.iconName.isEmpty() ? "file" : reg.iconName;
        }
    }
    return "file";
}

} // namespace acutesim::gui::core
