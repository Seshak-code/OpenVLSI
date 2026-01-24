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
    // Toggle logic: If clicking already active, logic might be to hide sidebar?
    // VS Code logic: Click active -> Toggle Sidebar visibility.
    // Click inactive -> Switch content.
    
    QString id = actionIds_[action];
    
    // NOTE: QButtonGroup with Exclusive=true enforces one always checked.
    // To support "Toggle Off", we might need logic in MainWindow or here.
    // For now, let's just emit the change. MainWindow can decide to hide dock 
    // if it receives the same ID again or if we handle toggle here.
    
    // Actually, typical QToolBar/Action behavior with exclusive group:
    // User clicks A (checked). User clicks A again -> stays checked.
    // To allow uncheck, we need custom handling or setExclusive(false) temporarily.
    
    emit activityChanged(id);
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
