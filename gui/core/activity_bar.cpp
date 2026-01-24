#include "activity_bar.h"
#include <QDebug>

namespace acutesim {
namespace gui {
namespace core {

ActivityBar::ActivityBar(QWidget* parent) : QToolBar(parent) {
    setOrientation(Qt::Vertical);
    setMovable(false);
    setFloatable(false);
    setIconSize(QSize(28, 28)); // Large icons
    
    // Exclusive selection group
    group_ = new QActionGroup(this);
    group_->setExclusive(true);
    
    updateStyle();
    
    // Connect to theme changes
    connect(&styling::ThemeManager::instance(), &styling::ThemeManager::themeChanged, 
            this, &ActivityBar::updateStyle);
}

QAction* ActivityBar::addActivity(const QString& id, const QIcon& icon, const QString& tooltip) {
    QAction* action = addAction(icon, "");
    action->setToolTip(tooltip);
    action->setCheckable(true);
    action->setData(id);
    
    group_->addAction(action);
    actionIds_[action] = id;
    
    connect(action, &QAction::triggered, this, [this, action]() {
        onActionTriggered(action);
    });
    
    return action;
}

void ActivityBar::setActive(const QString& id) {
    for (auto it = actionIds_.begin(); it != actionIds_.end(); ++it) {
        if (it.value() == id) {
            it.key()->setChecked(true);
            activeAction_ = it.key();
            return;
        }
    }
    
    if (id.isEmpty() && group_->checkedAction()) {
        group_->setExclusive(false);
        group_->checkedAction()->setChecked(false);
        group_->setExclusive(true);
        activeAction_ = nullptr;
    }
}

void ActivityBar::onActionTriggered(QAction* action) {
    // VS Code-style toggle logic:
    // - Click active -> Hide sidebar (toggle off)
    // - Click inactive -> Switch content (show that sidebar)

    QString id = actionIds_[action];

    if (action == activeAction_) {
        // Clicking already active item - toggle OFF (hide sidebar)
        // Temporarily disable exclusive mode to allow unchecking
        group_->setExclusive(false);
        action->setChecked(false);
        group_->setExclusive(true);
        activeAction_ = nullptr;

        // Emit empty ID to signal "hide sidebar"
        emit activityChanged(QString());
    } else {
        // Clicking different item - switch content
        activeAction_ = action;
        emit activityChanged(id);
    }
}

void ActivityBar::updateStyle() {
    auto& tm = styling::ThemeManager::instance();
    QString bg = tm.getColor("ui.header").name(); // Using header color (darker) or create activity.bar token
    QString border = tm.getColor("ui.border").name();
    
    // VS Code Activity Bar is usually #333333 (Dark) or #2C2C2C
    // Let's hardcode a dark aesthetic base or use a new token if available
    // For now, use 'ui.header' which is usually dark sidebar color.
    
    setStyleSheet(QString(
        "QToolBar { background-color: %1; border-right: 1px solid %2; spacing: 10px; padding-top: 10px; }"
        "QToolButton { border: none; background: transparent; padding: 10px; border-left: 2px solid transparent; }"
        "QToolButton:checked { border-left: 2px solid %3; background: #37373d; }" // Highlight active
        "QToolButton:hover { background: #2a2d2e; }"
    ).arg(bg).arg(border).arg(tm.getColor("ui.accent").name()));
}

} // namespace core
} // namespace gui
} // namespace acutesim
