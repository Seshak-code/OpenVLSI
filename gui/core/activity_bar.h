#pragma once

#include <QToolBar>
#include <QAction>
#include <QActionGroup>
#include <QMap>
#include <QIcon>
#include "../styling/theme_manager.h"

namespace acutesim {
namespace gui {
namespace core {

/**
 * @brief Left-side vertical toolbar acting as the primary navigation controller.
 * VS Code style "Activity Bar".
 */
class ActivityBar : public QToolBar {
    Q_OBJECT
public:
    explicit ActivityBar(QWidget* parent = nullptr);

    // Register a view (Sidebar Dock) to an icon
    QAction* addActivity(const QString& id, const QIcon& icon, const QString& tooltip);
    
    // Set active activity programmatically
    void setActive(const QString& id);

signals:
    void activityChanged(const QString& id);

private slots:
    void onActionTriggered(QAction* action);

private:
    QActionGroup* group_;
    QMap<QAction*, QString> actionIds_;
    QAction* activeAction_ = nullptr;
    
    void updateStyle();
};

} // namespace core
} // namespace gui
} // namespace acutesim
