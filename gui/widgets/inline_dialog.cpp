#include "inline_dialog.h"
#include "../styling/theme_manager.h"
#include <QPainter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QFileDialog>
#include <QApplication>

namespace acutesim::gui::widgets {

using TM = acutesim::gui::styling::ThemeManager;

// ============================================================================
// CONSTRUCTION
// ============================================================================

InlineDialog::InlineDialog(QWidget* parent,
                           DialogType type,
                           DialogLevel level,
                           const QString& title,
                           const QString& text,
                           DialogButtons buttons,
                           DialogCallback callback,
                           DialogButton defaultButton)
    : QWidget(parent)
    , type_(type)
    , level_(level)
    , title_(title)
    , text_(text)
    , buttons_(buttons)
    , callback_(std::move(callback))
    , defaultButton_(defaultButton)
{
    setObjectName("InlineDialog");
    setAttribute(Qt::WA_DeleteOnClose);
    setFocusPolicy(Qt::StrongFocus);

    // Cover entire parent
    if (parent) {
        resize(parent->size());
        parent->installEventFilter(this);
        raise();
    }

    buildUi();
    show();
    animateIn();

    // Auto-dismiss for fire-and-forget messages (info/warning/critical without callback)
    if (type_ == DialogType::Message && !callback_) {
        autoDismissTimer_ = new QTimer(this);
        autoDismissTimer_->setSingleShot(true);
        connect(autoDismissTimer_, &QTimer::timeout, this, [this]() {
            dismiss(DialogButton::Ok);
        });
        autoDismissTimer_->start(4000);
    }
}

InlineDialog::~InlineDialog() {
    if (auto* p = parentWidget()) {
        p->removeEventFilter(this);
    }
}

// ============================================================================
// STATIC CONVENIENCE METHODS
// ============================================================================

void InlineDialog::information(QWidget* parent, const QString& title, const QString& text, DialogCallback cb) {
    new InlineDialog(parent, DialogType::Message, DialogLevel::Info,
                     title, text, DialogButton::Ok, std::move(cb));
}

void InlineDialog::warning(QWidget* parent, const QString& title, const QString& text, DialogCallback cb) {
    new InlineDialog(parent, DialogType::Message, DialogLevel::Warning,
                     title, text, DialogButton::Ok, std::move(cb));
}

void InlineDialog::critical(QWidget* parent, const QString& title, const QString& text, DialogCallback cb) {
    new InlineDialog(parent, DialogType::Message, DialogLevel::Critical,
                     title, text, DialogButton::Ok, std::move(cb));
}

void InlineDialog::success(QWidget* parent, const QString& title, const QString& text, DialogCallback cb) {
    new InlineDialog(parent, DialogType::Message, DialogLevel::Success,
                     title, text, DialogButton::Ok, std::move(cb));
}

void InlineDialog::question(QWidget* parent, const QString& title, const QString& text,
                            DialogButtons buttons, DialogCallback cb, DialogButton defaultButton) {
    new InlineDialog(parent, DialogType::Confirmation, DialogLevel::Question,
                     title, text, buttons, std::move(cb), defaultButton);
}

void InlineDialog::getText(QWidget* parent, const QString& title, const QString& label,
                           const QString& defaultText, DialogCallback cb) {
    auto* dlg = new InlineDialog(parent, DialogType::TextInput, DialogLevel::Info,
                                 title, label,
                                 DialogButton::Ok | DialogButton::Cancel,
                                 std::move(cb), DialogButton::Ok);
    if (dlg->inputEdit_) {
        dlg->inputEdit_->setText(defaultText);
        dlg->inputEdit_->selectAll();
        dlg->inputEdit_->setFocus();
    }
}

void InlineDialog::getItem(QWidget* parent, const QString& title, const QString& label,
                           const QStringList& items, int defaultIndex, DialogCallback cb) {
    auto* dlg = new InlineDialog(parent, DialogType::ItemSelection, DialogLevel::Info,
                                 title, label,
                                 DialogButton::Ok | DialogButton::Cancel,
                                 std::move(cb), DialogButton::Ok);
    if (dlg->itemCombo_) {
        dlg->itemCombo_->addItems(items);
        if (defaultIndex >= 0 && defaultIndex < items.size())
            dlg->itemCombo_->setCurrentIndex(defaultIndex);
        dlg->itemCombo_->setFocus();
    }
}

void InlineDialog::showCustom(QWidget* parent, const QString& title, QWidget* content,
                              DialogButtons buttons, DialogCallback cb) {
    auto* dlg = new InlineDialog(parent, DialogType::CustomContent, DialogLevel::Info,
                                 title, QString(),
                                 buttons, std::move(cb), DialogButton::Ok);
    if (content && dlg->contentWidget_) {
        auto* lay = new QVBoxLayout(dlg->contentWidget_);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->addWidget(content);
    }
}

void InlineDialog::getOpenFileName(QWidget* parent, const QString& title,
                                   const QString& filter, DialogCallback cb) {
    auto* dlg = new QFileDialog(parent, title, QString(), filter);
    dlg->setFileMode(QFileDialog::ExistingFile);
    dlg->setOption(QFileDialog::DontUseNativeDialog, false);
    QObject::connect(dlg, &QFileDialog::fileSelected, parent, [cb](const QString& file) {
        if (cb) {
            DialogResult r;
            r.clickedButton = DialogButton::Ok;
            r.textValue = file;
            cb(r);
        }
    });
    QObject::connect(dlg, &QFileDialog::rejected, parent, [cb]() {
        if (cb) {
            DialogResult r;
            r.clickedButton = DialogButton::Cancel;
            cb(r);
        }
    });
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->open();
}

void InlineDialog::getSaveFileName(QWidget* parent, const QString& title,
                                   const QString& filter, DialogCallback cb) {
    auto* dlg = new QFileDialog(parent, title, QString(), filter);
    dlg->setAcceptMode(QFileDialog::AcceptSave);
    dlg->setOption(QFileDialog::DontUseNativeDialog, false);
    QObject::connect(dlg, &QFileDialog::fileSelected, parent, [cb](const QString& file) {
        if (cb) {
            DialogResult r;
            r.clickedButton = DialogButton::Ok;
            r.textValue = file;
            cb(r);
        }
    });
    QObject::connect(dlg, &QFileDialog::rejected, parent, [cb]() {
        if (cb) {
            DialogResult r;
            r.clickedButton = DialogButton::Cancel;
            cb(r);
        }
    });
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->open();
}

// ============================================================================
// UI BUILDING
// ============================================================================

void InlineDialog::buildUi() {
    // Bubble card
    bubbleCard_ = new QWidget(this);
    bubbleCard_->setObjectName("InlineDialogBubble");

    auto& tm = TM::instance();
    QString bgColor = tm.getColor("dialog.background").name();
    QString borderColor = tm.getColor("dialog.border").name();
    bubbleCard_->setStyleSheet(
        QString("QWidget#InlineDialogBubble { background: %1; border: 1px solid %2; border-radius: 8px; }")
        .arg(bgColor, borderColor));

    // Bubble opacity effect
    bubbleOpacity_ = new QGraphicsOpacityEffect(bubbleCard_);
    bubbleOpacity_->setOpacity(0.0);
    bubbleCard_->setGraphicsEffect(bubbleOpacity_);

    auto* mainLayout = new QVBoxLayout(bubbleCard_);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(12);

    // Title row (icon + title)
    auto* titleRow = new QHBoxLayout();
    titleRow->setSpacing(8);

    iconLabel_ = new QLabel(levelIcon(), bubbleCard_);
    iconLabel_->setStyleSheet(QString("font-size: 18px; color: %1;").arg(levelIconColor().name()));
    iconLabel_->setFixedWidth(24);
    titleRow->addWidget(iconLabel_);

    titleLabel_ = new QLabel(title_, bubbleCard_);
    titleLabel_->setStyleSheet(
        QString("font-size: 14px; font-weight: bold; color: %1;")
        .arg(tm.getColor("ui.text").name()));
    titleLabel_->setWordWrap(true);
    titleRow->addWidget(titleLabel_, 1);
    mainLayout->addLayout(titleRow);

    // Text label
    if (!text_.isEmpty()) {
        textLabel_ = new QLabel(text_, bubbleCard_);
        textLabel_->setStyleSheet(
            QString("font-size: 12px; color: %1;")
            .arg(tm.getColor("ui.textDim").name()));
        textLabel_->setWordWrap(true);
        mainLayout->addWidget(textLabel_);
    }

    // Type-specific content
    switch (type_) {
    case DialogType::TextInput: {
        inputEdit_ = new QLineEdit(bubbleCard_);
        inputEdit_->setStyleSheet(
            QString("QLineEdit { background: %1; color: %2; border: 1px solid %3; "
                    "border-radius: 4px; padding: 6px; font-size: 12px; }")
            .arg(tm.getColor("input.background").name(),
                 tm.getColor("input.text").name(),
                 tm.getColor("ui.border").name()));
        mainLayout->addWidget(inputEdit_);
        connect(inputEdit_, &QLineEdit::returnPressed, this, [this]() {
            dismiss(DialogButton::Ok);
        });
        break;
    }
    case DialogType::ItemSelection: {
        itemCombo_ = new QComboBox(bubbleCard_);
        itemCombo_->setStyleSheet(
            QString("QComboBox { background: %1; color: %2; border: 1px solid %3; "
                    "border-radius: 4px; padding: 6px; font-size: 12px; }")
            .arg(tm.getColor("input.background").name(),
                 tm.getColor("input.text").name(),
                 tm.getColor("ui.border").name()));
        mainLayout->addWidget(itemCombo_);
        break;
    }
    case DialogType::CustomContent: {
        contentWidget_ = new QWidget(bubbleCard_);
        mainLayout->addWidget(contentWidget_, 1);
        break;
    }
    default:
        break;
    }

    // Button row
    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(8);
    btnRow->addStretch();

    // Add buttons in logical order
    auto tryAdd = [&](DialogButton b, bool primary) {
        if (buttons_.testFlag(b)) addButton(b, primary);
    };

    // Secondary buttons first (left), primary last (right)
    tryAdd(DialogButton::Cancel, false);
    tryAdd(DialogButton::No, false);
    tryAdd(DialogButton::Discard, false);
    tryAdd(DialogButton::Ok, true);
    tryAdd(DialogButton::Yes, true);
    tryAdd(DialogButton::Save, true);

    for (auto* btn : actionButtons_) {
        btnRow->addWidget(btn);
    }
    mainLayout->addLayout(btnRow);

    // Set max width based on type
    int maxW = (type_ == DialogType::CustomContent) ? 600 : 420;
    bubbleCard_->setMaximumWidth(maxW);

    layoutBubble();
    setFocus();
}

void InlineDialog::addButton(DialogButton btn, bool primary) {
    auto& tm = TM::instance();
    auto* pb = new QPushButton(buttonLabel(btn), bubbleCard_);
    pb->setFixedHeight(30);
    pb->setMinimumWidth(80);

    if (primary) {
        pb->setStyleSheet(
            QString("QPushButton { background: %1; color: white; border: none; "
                    "border-radius: 4px; padding: 4px 16px; font-size: 12px; }"
                    "QPushButton:hover { background: %2; }")
            .arg(tm.getColor("dialog.buttonPrimary").name(),
                 tm.getColor("dialog.buttonPrimary").lighter(120).name()));
        if (btn == defaultButton_) {
            pb->setDefault(true);
        }
    } else {
        QString borderCol = tm.getColor("dialog.border").name();
        if (btn == DialogButton::Cancel) {
            pb->setStyleSheet(
                QString("QPushButton { background: transparent; color: %1; border: none; "
                        "border-radius: 4px; padding: 4px 16px; font-size: 12px; }"
                        "QPushButton:hover { background: rgba(255,255,255,0.05); }")
                .arg(tm.getColor("ui.textDim").name()));
        } else {
            pb->setStyleSheet(
                QString("QPushButton { background: transparent; color: %1; border: 1px solid %2; "
                        "border-radius: 4px; padding: 4px 16px; font-size: 12px; }"
                        "QPushButton:hover { background: rgba(255,255,255,0.05); }")
                .arg(tm.getColor("ui.text").name(), borderCol));
        }
    }

    connect(pb, &QPushButton::clicked, this, [this, btn]() { dismiss(btn); });
    actionButtons_.append(pb);
}

QString InlineDialog::buttonLabel(DialogButton btn) const {
    switch (btn) {
    case DialogButton::Ok:      return "OK";
    case DialogButton::Cancel:  return "Cancel";
    case DialogButton::Save:    return "Save";
    case DialogButton::Discard: return "Discard";
    case DialogButton::Yes:     return "Yes";
    case DialogButton::No:      return "No";
    }
    return {};
}

QString InlineDialog::levelIcon() const {
    switch (level_) {
    case DialogLevel::Info:     return QString::fromUtf8("\xe2\x84\xb9");       // ℹ
    case DialogLevel::Success:  return QString::fromUtf8("\xe2\x9c\x93");       // ✓
    case DialogLevel::Warning:  return QString::fromUtf8("\xe2\x9a\xa0");       // ⚠
    case DialogLevel::Error:
    case DialogLevel::Critical: return QString::fromUtf8("\xe2\x9c\x95");       // ✕
    case DialogLevel::Question: return QString::fromUtf8("?");
    }
    return {};
}

QColor InlineDialog::levelIconColor() const {
    auto& tm = TM::instance();
    switch (level_) {
    case DialogLevel::Info:     return tm.getColor("ui.accent");
    case DialogLevel::Success:  return QColor("#4ec9b0");
    case DialogLevel::Warning:  return QColor("#dcdcaa");
    case DialogLevel::Error:
    case DialogLevel::Critical: return tm.getColor("ui.error");
    case DialogLevel::Question: return tm.getColor("ui.accent");
    }
    return tm.getColor("ui.text");
}

// ============================================================================
// ANIMATION
// ============================================================================

void InlineDialog::animateIn() {
    showAnim_ = new QParallelAnimationGroup(this);

    // Scrim fade in
    auto* scrimAnim = new QPropertyAnimation(this, "scrimOpacity", this);
    scrimAnim->setDuration(200);
    scrimAnim->setStartValue(0.0);
    scrimAnim->setEndValue(1.0);
    scrimAnim->setEasingCurve(QEasingCurve::OutQuad);
    showAnim_->addAnimation(scrimAnim);

    // Bubble scale pop
    auto* scaleAnim = new QPropertyAnimation(this, "bubbleScale", this);
    scaleAnim->setDuration(280);
    scaleAnim->setStartValue(0.75);
    scaleAnim->setEndValue(1.0);
    scaleAnim->setEasingCurve(QEasingCurve::OutBack);
    showAnim_->addAnimation(scaleAnim);

    // Bubble opacity
    auto* opAnim = new QPropertyAnimation(bubbleOpacity_, "opacity", this);
    opAnim->setDuration(200);
    opAnim->setStartValue(0.0);
    opAnim->setEndValue(1.0);
    opAnim->setEasingCurve(QEasingCurve::OutQuad);
    showAnim_->addAnimation(opAnim);

    showAnim_->start(QAbstractAnimation::DeleteWhenStopped);
}

void InlineDialog::animateOut() {
    if (dismissed_) return;
    dismissed_ = true;

    if (autoDismissTimer_) autoDismissTimer_->stop();

    hideAnim_ = new QParallelAnimationGroup(this);

    auto* scrimAnim = new QPropertyAnimation(this, "scrimOpacity", this);
    scrimAnim->setDuration(180);
    scrimAnim->setStartValue(scrimOpacity_);
    scrimAnim->setEndValue(0.0);
    scrimAnim->setEasingCurve(QEasingCurve::InQuad);
    hideAnim_->addAnimation(scrimAnim);

    auto* scaleAnim = new QPropertyAnimation(this, "bubbleScale", this);
    scaleAnim->setDuration(150);
    scaleAnim->setStartValue(bubbleScale_);
    scaleAnim->setEndValue(0.85);
    scaleAnim->setEasingCurve(QEasingCurve::InCubic);
    hideAnim_->addAnimation(scaleAnim);

    auto* opAnim = new QPropertyAnimation(bubbleOpacity_, "opacity", this);
    opAnim->setDuration(150);
    opAnim->setStartValue(bubbleOpacity_->opacity());
    opAnim->setEndValue(0.0);
    opAnim->setEasingCurve(QEasingCurve::InCubic);
    hideAnim_->addAnimation(opAnim);

    connect(hideAnim_, &QParallelAnimationGroup::finished, this, [this]() {
        emit finished(result_);
        if (callback_) callback_(result_);
        close();
    });

    hideAnim_->start(QAbstractAnimation::DeleteWhenStopped);
}

void InlineDialog::dismiss(DialogButton btn) {
    if (dismissed_) return;

    result_.clickedButton = btn;
    if (inputEdit_) result_.textValue = inputEdit_->text();
    if (itemCombo_) {
        result_.selectedIndex = itemCombo_->currentIndex();
        result_.textValue = itemCombo_->currentText();
    }

    animateOut();
}

// ============================================================================
// LAYOUT / PAINT
// ============================================================================

void InlineDialog::setBubbleScale(qreal s) {
    bubbleScale_ = s;
    layoutBubble();
}

void InlineDialog::setScrimOpacity(qreal o) {
    scrimOpacity_ = o;
    update();
}

void InlineDialog::layoutBubble() {
    if (!bubbleCard_) return;

    QSize hint = bubbleCard_->sizeHint();
    int w = qRound(hint.width() * bubbleScale_);
    int h = qRound(hint.height() * bubbleScale_);
    int x = (width() - w) / 2;
    int y = (height() - h) / 2;
    bubbleCard_->setGeometry(x, y, w, h);
}

void InlineDialog::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    QColor c = TM::instance().getColor("dialog.scrim");
    c.setAlphaF(scrimOpacity_ * 0.65);
    painter.fillRect(rect(), c);
}

// ============================================================================
// KEYBOARD / EVENT FILTER
// ============================================================================

void InlineDialog::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Escape:
        // Dismiss with Cancel > No > Ok (fallback order)
        if (buttons_.testFlag(DialogButton::Cancel))
            dismiss(DialogButton::Cancel);
        else if (buttons_.testFlag(DialogButton::No))
            dismiss(DialogButton::No);
        else
            dismiss(DialogButton::Ok);
        return;

    case Qt::Key_Return:
    case Qt::Key_Enter:
        dismiss(defaultButton_);
        return;

    case Qt::Key_Tab: {
        // Cycle focus through buttons
        if (actionButtons_.isEmpty()) break;
        int cur = -1;
        for (int i = 0; i < actionButtons_.size(); ++i) {
            if (actionButtons_[i]->hasFocus()) { cur = i; break; }
        }
        int next = (cur + 1) % actionButtons_.size();
        actionButtons_[next]->setFocus();
        event->accept();
        return;
    }
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}

bool InlineDialog::eventFilter(QObject* obj, QEvent* event) {
    if (obj == parentWidget() && event->type() == QEvent::Resize) {
        resize(parentWidget()->size());
        layoutBubble();
    }
    return QWidget::eventFilter(obj, event);
}

} // namespace acutesim::gui::widgets
