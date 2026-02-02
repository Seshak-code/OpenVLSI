#include "hierarchy_path.h"

/**
 * HierarchyPath - Manages the navigation stack for hierarchical schematics.
 * 
 * Invariants:
 * 1. Root depth == 0 (levels_.size() == 1)
 * 2. Depth == number of schematic hops (levels_.size() - 1)
 * 3. Breadcrumb length == depth + 1 (levels_.size())
 */
namespace acutesim::gui::core {

HierarchyPath::HierarchyPath(QObject* parent)
    : QObject(parent)
{
}

void HierarchyPath::setRoot(const QString& filePath, const QString& displayName) {
    std::optional<HierarchyLevel> previous;
    if (!levels_.isEmpty()) {
        previous = levels_.last();
    }

    levels_.clear();

    HierarchyLevel root;
    root.filePath = filePath;
    root.instanceName = QString();  // Root has no instance name
    root.displayName = displayName;
    root.viewportCenter = QPointF(0, 0);
    root.zoomLevel = 1.0;

    levels_.append(root);

    emit levelChanged(root, previous);
}

void HierarchyPath::descendInto(const QString& filePath, const QString& instanceName,
                                 const QString& displayName, QPointF viewportCenter, qreal zoomLevel) {
    // Update current level's viewport state before descending
    if (!levels_.isEmpty()) {
        levels_.last().viewportCenter = viewportCenter;
        levels_.last().zoomLevel = zoomLevel;
    }

    std::optional<HierarchyLevel> previous;
    if (!levels_.isEmpty()) {
        previous = levels_.last();
    }

    HierarchyLevel newLevel;
    newLevel.filePath = filePath;
    newLevel.instanceName = instanceName;
    newLevel.displayName = displayName;
    newLevel.viewportCenter = QPointF(0, 0);  // New view starts centered
    newLevel.zoomLevel = 1.0;

    levels_.append(newLevel);

    emit levelChanged(newLevel, previous);
}

std::optional<HierarchyLevel> HierarchyPath::ascend() {
    if (levels_.size() <= 1) {
        // Already at root, cannot ascend further
        return std::nullopt;
    }

    HierarchyLevel previous = levels_.last();
    levels_.removeLast();

    HierarchyLevel& current = levels_.last();
    emit levelChanged(current, previous);

    return current;
}

std::optional<HierarchyLevel> HierarchyPath::goToDepth(int depth) {
    if (depth < 0 || depth >= levels_.size()) {
        return std::nullopt;
    }

    if (depth == levels_.size() - 1) {
        // Already at this depth
        return levels_.at(depth);
    }

    std::optional<HierarchyLevel> previous = levels_.last();

    // Trim to target depth + 1 (keep levels 0..depth inclusive)
    while (levels_.size() > depth + 1) {
        levels_.removeLast();
    }

    HierarchyLevel& current = levels_.last();
    emit levelChanged(current, previous);

    return current;
}

void HierarchyPath::clear() {
    levels_.clear();
    emit cleared();
}

QString HierarchyPath::currentFilePath() const {
    if (levels_.isEmpty()) {
        return QString();
    }
    return levels_.last().filePath;
}

QString HierarchyPath::currentDisplayName() const {
    if (levels_.isEmpty()) {
        return QString();
    }
    return levels_.last().displayName;
}

QStringList HierarchyPath::getInstancePath() const {
    QStringList path;
    for (const auto& l : levels_) {
        if (!l.instanceName.isEmpty()) {
            path << l.instanceName;
        }
    }
    return path;
}

} // namespace acutesim::gui::core
