#include "keybinding_manager.h"
#include <QWidget>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

namespace acutesim {
namespace gui {
namespace core {

KeybindingManager& KeybindingManager::instance() {
    static KeybindingManager instance;
    return instance;
}

KeybindingManager::KeybindingManager()
    : settings_(QSettings::IniFormat, QSettings::UserScope, "AcuteSim", "Keybindings")
{
    loadCustomizations();
}

void KeybindingManager::registerAction(const QString& id, const QString& label,
                                       const QString& category, const QKeySequence& defaultKey,
                                       QAction* action, const QString& context) {
    KeyBinding binding;
    binding.id = id;
    binding.label = label;
    binding.category = category;
    binding.defaultKey = defaultKey;
    binding.context = context;
    binding.isCustomized = false;

    // Check if user has customized this binding
    QString savedKey = settings_.value("bindings/" + id).toString();
    if (!savedKey.isEmpty()) {
        binding.currentKey = QKeySequence(savedKey);
        binding.isCustomized = (binding.currentKey != binding.defaultKey);
    } else {
        binding.currentKey = defaultKey;
    }

    bindings_[id] = binding;
    actions_[id] = action;

    // Apply the shortcut to the action
    if (action) {
        action->setShortcut(binding.currentKey);
    }

    qDebug() << "[KeybindingManager] Registered:" << id << "->" << binding.currentKey.toString();
}

void KeybindingManager::registerCallback(const QString& id, const QString& label,
                                         const QString& category, const QKeySequence& defaultKey,
                                         std::function<void()> callback, QWidget* parent,
                                         const QString& context) {
    KeyBinding binding;
    binding.id = id;
    binding.label = label;
    binding.category = category;
    binding.defaultKey = defaultKey;
    binding.context = context;
    binding.isCustomized = false;

    // Check if user has customized this binding
    QString savedKey = settings_.value("bindings/" + id).toString();
    if (!savedKey.isEmpty()) {
        binding.currentKey = QKeySequence(savedKey);
        binding.isCustomized = (binding.currentKey != binding.defaultKey);
    } else {
        binding.currentKey = defaultKey;
    }

    bindings_[id] = binding;
    callbacks_[id] = callback;

    // Create a QShortcut for the callback
    QShortcut* shortcut = new QShortcut(binding.currentKey, parent);
    connect(shortcut, &QShortcut::activated, [callback]() {
        if (callback) callback();
    });
    shortcuts_[id] = shortcut;

    qDebug() << "[KeybindingManager] Registered callback:" << id << "->" << binding.currentKey.toString();
}

bool KeybindingManager::setKeybinding(const QString& id, const QKeySequence& newKey) {
    if (!bindings_.contains(id)) {
        qDebug() << "[KeybindingManager] Unknown binding:" << id;
        return false;
    }

    // Check for conflicts
    QString conflict = findConflict(newKey, bindings_[id].context);
    if (!conflict.isEmpty() && conflict != id) {
        qDebug() << "[KeybindingManager] Conflict with:" << conflict;
        return false;
    }

    QKeySequence oldKey = bindings_[id].currentKey;
    bindings_[id].currentKey = newKey;
    bindings_[id].isCustomized = (newKey != bindings_[id].defaultKey);

    // Apply the change
    applyBinding(id);
    saveCustomizations();

    emit keybindingChanged(id, oldKey, newKey);
    qDebug() << "[KeybindingManager] Changed" << id << "from" << oldKey.toString() << "to" << newKey.toString();
    return true;
}

bool KeybindingManager::resetToDefault(const QString& id) {
    if (!bindings_.contains(id)) {
        return false;
    }

    QKeySequence oldKey = bindings_[id].currentKey;
    bindings_[id].currentKey = bindings_[id].defaultKey;
    bindings_[id].isCustomized = false;

    applyBinding(id);
    settings_.remove("bindings/" + id);
    settings_.sync();

    emit keybindingChanged(id, oldKey, bindings_[id].defaultKey);
    return true;
}

void KeybindingManager::resetAllToDefaults() {
    for (auto& binding : bindings_) {
        binding.currentKey = binding.defaultKey;
        binding.isCustomized = false;
        applyBinding(binding.id);
    }

    settings_.remove("bindings");
    settings_.sync();

    emit keybindingsReset();
    qDebug() << "[KeybindingManager] All keybindings reset to defaults";
}

QList<KeyBinding> KeybindingManager::getAllBindings() const {
    return bindings_.values();
}

QList<KeyBinding> KeybindingManager::getBindingsForCategory(const QString& category) const {
    QList<KeyBinding> result;
    for (const auto& binding : bindings_) {
        if (binding.category == category) {
            result.append(binding);
        }
    }
    return result;
}

QList<QString> KeybindingManager::getCategories() const {
    QSet<QString> categories;
    for (const auto& binding : bindings_) {
        categories.insert(binding.category);
    }
    return categories.values();
}

KeyBinding KeybindingManager::getBinding(const QString& id) const {
    return bindings_.value(id);
}

QString KeybindingManager::findConflict(const QKeySequence& key, const QString& context) const {
    if (key.isEmpty()) {
        return QString();
    }

    for (const auto& binding : bindings_) {
        if (binding.currentKey == key) {
            // Check context compatibility
            if (context == "global" || binding.context == "global" || binding.context == context) {
                return binding.id;
            }
        }
    }
    return QString();
}

bool KeybindingManager::hasConflict(const QKeySequence& key, const QString& context) const {
    return !findConflict(key, context).isEmpty();
}

QList<KeyBinding> KeybindingManager::search(const QString& query) const {
    QList<KeyBinding> results;
    QString lowerQuery = query.toLower();

    for (const auto& binding : bindings_) {
        if (binding.id.toLower().contains(lowerQuery) ||
            binding.label.toLower().contains(lowerQuery) ||
            binding.category.toLower().contains(lowerQuery) ||
            binding.currentKey.toString().toLower().contains(lowerQuery)) {
            results.append(binding);
        }
    }
    return results;
}

QString KeybindingManager::exportToJson() const {
    QJsonObject root;
    QJsonArray bindingsArray;

    for (const auto& binding : bindings_) {
        if (binding.isCustomized) {
            QJsonObject obj;
            obj["id"] = binding.id;
            obj["key"] = binding.currentKey.toString();
            bindingsArray.append(obj);
        }
    }

    root["version"] = 1;
    root["bindings"] = bindingsArray;

    QJsonDocument doc(root);
    return doc.toJson(QJsonDocument::Indented);
}

bool KeybindingManager::importFromJson(const QString& json) {
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (doc.isNull() || !doc.isObject()) {
        return false;
    }

    QJsonObject root = doc.object();
    QJsonArray bindingsArray = root["bindings"].toArray();

    for (const auto& val : bindingsArray) {
        QJsonObject obj = val.toObject();
        QString id = obj["id"].toString();
        QString key = obj["key"].toString();

        if (!id.isEmpty() && bindings_.contains(id)) {
            setKeybinding(id, QKeySequence(key));
        }
    }

    return true;
}

void KeybindingManager::loadCustomizations() {
    // Customizations are loaded per-binding in registerAction/registerCallback
    qDebug() << "[KeybindingManager] Customizations will be loaded on registration";
}

void KeybindingManager::saveCustomizations() {
    for (const auto& binding : bindings_) {
        if (binding.isCustomized) {
            settings_.setValue("bindings/" + binding.id, binding.currentKey.toString());
        }
    }
    settings_.sync();
}

void KeybindingManager::applyBinding(const QString& id) {
    if (!bindings_.contains(id)) return;

    const KeyBinding& binding = bindings_[id];

    // Apply to QAction if registered
    if (actions_.contains(id) && actions_[id]) {
        actions_[id]->setShortcut(binding.currentKey);
    }

    // Apply to QShortcut if registered
    if (shortcuts_.contains(id) && shortcuts_[id]) {
        shortcuts_[id]->setKey(binding.currentKey);
    }
}

} // namespace core
} // namespace gui
} // namespace acutesim
