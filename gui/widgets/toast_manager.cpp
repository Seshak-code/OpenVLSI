#include "toast_manager.h"
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QStyle>
#include "../../gui/styling/theme_manager.h"

namespace acutesim {
namespace gui {
namespace widgets {

ToastManager& ToastManager::instance() {
    static ToastManager inst;
    return inst;
}

ToastManager::ToastManager(QObject* parent) : QObject(parent) {
    fadeTimer_ = new QTimer(this);
    fadeTimer_->setSingleShot(true);
    connect(fadeTimer_, &QTimer::timeout, this, &ToastManager::fadeOut);
}

void ToastManager::setRootWidget(QWidget* root) {
    rootWidget_ = root;
}

void ToastManager::showToast(const QString& message, ToastLevel level) {
    queue_.enqueue({message, level});
    if (!isShowing_) {
        processQueue();
    }
}

void ToastManager::processQueue() {
    if (queue_.isEmpty() || !rootWidget_) {
        isShowing_ = false;
        return;
    }

    isShowing_ = true;
    ToastEntry entry = queue_.dequeue();
    
    // Create Toast Label
    if (!currentToast_) {
        currentToast_ = new QLabel(rootWidget_);
        currentToast_->setAlignment(Qt::AlignCenter);
    }
    
    // Style based on Level
    QString bgColor, textColor, borderColor;
    int duration = 3000;
    
    switch(entry.level) {
        case ToastLevel::Success: 
            bgColor = "#206020"; textColor = "white"; borderColor = "#40a040";
            break;
        case ToastLevel::Warning:
            bgColor = "#606020"; textColor = "white"; borderColor = "#a0a040";
            duration = 5000;
            break;
        case ToastLevel::Error:
        case ToastLevel::Critical:
            bgColor = "#602020"; textColor = "white"; borderColor = "#a04040";
            duration = 7000;
            break;
        default: // Info
            bgColor = "#333333"; textColor = "white"; borderColor = "#555555";
            break;
    }
    
    currentToast_->setText(entry.message);
    currentToast_->setStyleSheet(QString(
        "background-color: %1; color: %2; border: 1px solid %3; "
        "border-radius: 4px; padding: 8px 16px; font-weight: bold;"
    ).arg(bgColor).arg(textColor).arg(borderColor));
    
    currentToast_->adjustSize();
    
    // Position: Bottom Center
    int x = (rootWidget_->width() - currentToast_->width()) / 2;
    int y = rootWidget_->height() - currentToast_->height() - 50;
    currentToast_->move(x, y);
    
    currentToast_->show();
    currentToast_->raise();
    
    // Fade In Animation
    auto effect = new QGraphicsOpacityEffect(currentToast_);
    currentToast_->setGraphicsEffect(effect);
    auto anim = new QPropertyAnimation(effect, "opacity");
    anim->setDuration(200);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
    
    // Schedule Fade Out
    fadeTimer_->start(duration);
}

void ToastManager::fadeOut() {
    if (!currentToast_) return;
    
    auto effect = new QGraphicsOpacityEffect(currentToast_);
    currentToast_->setGraphicsEffect(effect);
    auto anim = new QPropertyAnimation(effect, "opacity");
    anim->setDuration(200);
    anim->setStartValue(1.0);
    anim->setEndValue(0.0);
    connect(anim, &QAbstractAnimation::finished, [this]() {
        currentToast_->hide();
        processQueue(); // Show next
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

} // namespace widgets
} // namespace gui
} // namespace acutesim
