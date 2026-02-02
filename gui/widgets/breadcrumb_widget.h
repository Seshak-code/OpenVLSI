#pragma once

#include <QWidget>
#include <QHBoxLayout>
#include <QToolButton>
#include <QLabel>
#include <QVector>
#include "../core/hierarchy_path.h"

namespace acutesim::gui::widgets {

/**
 * @brief Visual breadcrumb navigation widget for hierarchical schematics.
 *
 * Displays the current navigation path as clickable segments separated
 * by ">" chevrons. Auto-hides when at root level (depth == 0).
 *
 * Example appearance: [<] Top > Block1 > InnerBlock
 */
class BreadcrumbWidget : public QWidget {
    Q_OBJECT
public:
    explicit BreadcrumbWidget(QWidget* parent = nullptr);
    ~BreadcrumbWidget() override = default;

    /**
     * @brief Connect to a HierarchyPath to track navigation state.
     * @param path The hierarchy path to observe.
     */
    void setHierarchyPath(acutesim::gui::core::HierarchyPath* path);

    /**
     * @brief Manually rebuild the breadcrumb UI from the current path.
     */
    void rebuild();

signals:
    /**
     * @brief Emitted when user clicks a breadcrumb segment.
     * @param depth The depth level to navigate to (0 = root).
     */
    void navigationRequested(int depth);

private slots:
    void onLevelChanged(const acutesim::gui::core::HierarchyLevel& newLevel,
                        std::optional<acutesim::gui::core::HierarchyLevel> previous);
    void onCleared();

private:
    void clearSegments();
    QToolButton* createSegmentButton(const QString& text, int depth);
    QLabel* createSeparator();
    void updateVisibility();

    acutesim::gui::core::HierarchyPath* hierarchyPath_ = nullptr;
    QHBoxLayout* layout_ = nullptr;
    QToolButton* backButton_ = nullptr;
    QVector<QWidget*> segments_;  // Buttons and separators
};

} // namespace acutesim::gui::widgets
