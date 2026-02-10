#include "inline_progress.h"
#include "../styling/theme_manager.h"
#include <QPainter>
#include <QVBoxLayout>
#include <QKeyEvent>

namespace acutesim::gui::widgets {

using TM = acutesim::gui::styling::ThemeManager;

InlineProgress::InlineProgress(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("InlineProgress");
    setAttribute(Qt::WA_DeleteOnClose);
    setFocusPolicy(Qt::StrongFocus);

    if (parent) {
        resize(parent->size());
        parent->installEventFilter(this);
        raise();
    }

    buildUi();
}

InlineProgress::~InlineProgress() {
    if (auto* p = parentWidget()) {
        p->removeEventFilter(this);
    }
}

void InlineProgress::buildUi() {
    auto& tm = TM::instance();

    bubbleCard_ = new QWidget(this);
    bubbleCard_->setObjectName("InlineProgressBubble");

    QString bgColor = tm.getColor("dialog.background").name();
    QString borderColor = tm.getColor("dialog.border").name();
    bubbleCard_->setStyleSheet(
        QString("QWidget#InlineProgressBubble { background: %1; border: 1px solid %2; border-radius: 8px; }")
        .arg(bgColor, borderColor));

    bubbleOpacity_ = new QGraphicsOpacityEffect(bubbleCard_);
    bubbleOpacity_->setOpacity(0.0);
    bubbleCard_->setGraphicsEffect(bubbleOpacity_);

    auto* layout = new QVBoxLayout(bubbleCard_);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    titleLabel_ = new QLabel("Processing...", bubbleCard_);
    titleLabel_->setStyleSheet(
        QString("font-size: 14px; font-weight: bold; color: %1;")
        .arg(tm.getColor("ui.text").name()));
    layout->addWidget(titleLabel_);

    progressBar_ = new QProgressBar(bubbleCard_);
    progressBar_->setTextVisible(true);
    progressBar_->setStyleSheet(
        QString("QProgressBar { background: %1; border: 1px solid %2; border-radius: 4px; height: 20px; text-align: center; color: %3; }"
                "QProgressBar::chunk { background: %4; border-radius: 3px; }")
        .arg(tm.getColor("input.background").name(),
             tm.getColor("ui.border").name(),
             tm.getColor("ui.text").name(),
             tm.getColor("dialog.buttonPrimary").name()));
    layout->addWidget(progressBar_);

    cancelBtn_ = new QPushButton("Cancel", bubbleCard_);
    cancelBtn_->setFixedHeight(30);
    cancelBtn_->setStyleSheet(
        QString("QPushButton { background: transparent; color: %1; border: 1px solid %2; "
                "border-radius: 4px; padding: 4px 16px; font-size: 12px; }"
                "QPushButton:hover { background: rgba(255,255,255,0.05); }")
        .arg(tm.getColor("ui.text").name(),
             tm.getColor("dialog.border").name()));
    cancelBtn_->hide();
    connect(cancelBtn_, &QPushButton::clicked, this, [this]() {
        emit cancelled();
    });

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    btnRow->addWidget(cancelBtn_);
    layout->addLayout(btnRow);

    bubbleCard_->setMaximumWidth(380);
    layoutBubble();
}

void InlineProgress::setTitle(const QString& title) {
    if (titleLabel_) titleLabel_->setText(title);
}

void InlineProgress::setRange(int min, int max) {
    if (progressBar_) progressBar_->setRange(min, max);
}

void InlineProgress::setValue(int value) {
    if (progressBar_) progressBar_->setValue(value);
}

void InlineProgress::setCancelEnabled(bool enabled) {
    if (cancelBtn_) cancelBtn_->setVisible(enabled);
}

void InlineProgress::show() {
    QWidget::show();
    raise();
    animateIn();
}

void InlineProgress::dismiss() {
    if (dismissed_) return;
    dismissed_ = true;
    animateOut();
}

// ============================================================================
// ANIMATION
// ============================================================================

void InlineProgress::animateIn() {
    auto* group = new QParallelAnimationGroup(this);

    auto* scrimAnim = new QPropertyAnimation(this, "scrimOpacity", this);
    scrimAnim->setDuration(200);
    scrimAnim->setStartValue(0.0);
    scrimAnim->setEndValue(1.0);
    scrimAnim->setEasingCurve(QEasingCurve::OutQuad);
    group->addAnimation(scrimAnim);

    auto* scaleAnim = new QPropertyAnimation(this, "bubbleScale", this);
    scaleAnim->setDuration(280);
    scaleAnim->setStartValue(0.75);
    scaleAnim->setEndValue(1.0);
    scaleAnim->setEasingCurve(QEasingCurve::OutBack);
    group->addAnimation(scaleAnim);

    auto* opAnim = new QPropertyAnimation(bubbleOpacity_, "opacity", this);
    opAnim->setDuration(200);
    opAnim->setStartValue(0.0);
    opAnim->setEndValue(1.0);
    opAnim->setEasingCurve(QEasingCurve::OutQuad);
    group->addAnimation(opAnim);

    group->start(QAbstractAnimation::DeleteWhenStopped);
}

void InlineProgress::animateOut() {
    auto* group = new QParallelAnimationGroup(this);

    auto* scrimAnim = new QPropertyAnimation(this, "scrimOpacity", this);
    scrimAnim->setDuration(180);
    scrimAnim->setStartValue(scrimOpacity_);
    scrimAnim->setEndValue(0.0);
    scrimAnim->setEasingCurve(QEasingCurve::InQuad);
    group->addAnimation(scrimAnim);

    auto* scaleAnim = new QPropertyAnimation(this, "bubbleScale", this);
    scaleAnim->setDuration(150);
    scaleAnim->setStartValue(bubbleScale_);
    scaleAnim->setEndValue(0.85);
    scaleAnim->setEasingCurve(QEasingCurve::InCubic);
    group->addAnimation(scaleAnim);

    auto* opAnim = new QPropertyAnimation(bubbleOpacity_, "opacity", this);
    opAnim->setDuration(150);
    opAnim->setStartValue(bubbleOpacity_->opacity());
    opAnim->setEndValue(0.0);
    opAnim->setEasingCurve(QEasingCurve::InCubic);
    group->addAnimation(opAnim);

    connect(group, &QParallelAnimationGroup::finished, this, [this]() {
        close();
    });

    group->start(QAbstractAnimation::DeleteWhenStopped);
}

// ============================================================================
// LAYOUT / PAINT
// ============================================================================

void InlineProgress::setBubbleScale(qreal s) {
    bubbleScale_ = s;
    layoutBubble();
}

void InlineProgress::setScrimOpacity(qreal o) {
    scrimOpacity_ = o;
    update();
}

void InlineProgress::layoutBubble() {
    if (!bubbleCard_) return;
    QSize hint = bubbleCard_->sizeHint();
    int w = qRound(hint.width() * bubbleScale_);
    int h = qRound(hint.height() * bubbleScale_);
    int x = (width() - w) / 2;
    int y = (height() - h) / 2;
    bubbleCard_->setGeometry(x, y, w, h);
}

void InlineProgress::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    QColor c = TM::instance().getColor("dialog.scrim");
    c.setAlphaF(scrimOpacity_ * 0.65);
    painter.fillRect(rect(), c);
}

bool InlineProgress::eventFilter(QObject* obj, QEvent* event) {
    if (obj == parentWidget() && event->type() == QEvent::Resize) {
        resize(parentWidget()->size());
        layoutBubble();
    }
    return QWidget::eventFilter(obj, event);
}

} // namespace acutesim::gui::widgets
