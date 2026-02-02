#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QPointF>
#include <optional>

namespace acutesim::gui::core {

/**
 * @brief Represents a single level in the hierarchy navigation stack.
 * Stores viewport state for restoration when navigating back up.
 */
struct HierarchyLevel {
    QString filePath;       // Path to the schematic file
    QString instanceName;   // Instance name in parent (empty for root)
    QString displayName;    // Human-readable name for breadcrumb
    QPointF viewportCenter; // Viewport center for state restoration
    qreal zoomLevel = 1.0;  // Zoom level for state restoration
};

/**
 * @brief Manages hierarchical navigation state for a schematic editor.
 *
 * Each SchematicEditor owns one HierarchyPath instance. The path tracks
 * the navigation stack as users drill down into subcircuits and provides
 * signals for UI updates (breadcrumb widget).
 *
 * Navigation is excluded from undo/redo as it's view-level, not data mutation.
 */
class HierarchyPath : public QObject {
    Q_OBJECT
public:
    explicit HierarchyPath(QObject* parent = nullptr);
    ~HierarchyPath() override = default;

    /**
     * @brief Initialize or reset to root level.
     * @param filePath Path to the root schematic file.
     * @param displayName Human-readable name (typically filename).
     */
    void setRoot(const QString& filePath, const QString& displayName);

    /**
     * @brief Push a new level onto the hierarchy stack (drill down).
     * @param filePath Path to the subcircuit schematic.
     * @param instanceName Instance name in the current schematic.
     * @param displayName Human-readable name for breadcrumb.
     * @param viewportCenter Current viewport center (for restoration).
     * @param zoomLevel Current zoom level (for restoration).
     */
    void descendInto(const QString& filePath, const QString& instanceName,
                     const QString& displayName, QPointF viewportCenter, qreal zoomLevel);

    /**
     * @brief Pop the current level and return to parent.
     * @return The level we're returning to, or nullopt if already at root.
     */
    std::optional<HierarchyLevel> ascend();

    /**
     * @brief Navigate directly to a specific depth in the hierarchy.
     * @param depth Target depth (0 = root).
     * @return The level at the target depth, or nullopt if invalid.
     */
    std::optional<HierarchyLevel> goToDepth(int depth);

    /**
     * @brief Clear the hierarchy path entirely.
     */
    void clear();

    // Queries
    int depth() const { return levels_.size() - 1; }  // 0 = root
    bool isAtRoot() const { return levels_.size() <= 1; }
    QString currentFilePath() const;
    QString currentDisplayName() const;
    const QVector<HierarchyLevel>& levels() const { return levels_; }

    /**
     * @brief Get the full hierarchical instance path (e.g. ["X1", "X2"]).
     */
    QStringList getInstancePath() const;

signals:
    /**
     * @brief Emitted when the current level changes.
     * @param newLevel The new current level.
     * @param previous The previous level (nullopt if navigating to root from empty).
     */
    void levelChanged(const HierarchyLevel& newLevel, std::optional<HierarchyLevel> previous);

    /**
     * @brief Emitted when the path is cleared entirely.
     */
    void cleared();

private:
    QVector<HierarchyLevel> levels_;
};

} // namespace acutesim::gui::core
