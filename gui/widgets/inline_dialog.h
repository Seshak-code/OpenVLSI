#pragma once

#include "inline_dialog_types.h"
#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QGraphicsOpacityEffect>
#include <QTimer>

namespace acutesim::gui::widgets {

class InlineDialog : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal bubbleScale READ bubbleScale WRITE setBubbleScale)
    Q_PROPERTY(qreal scrimOpacity READ scrimOpacity WRITE setScrimOpacity)

public:
    explicit InlineDialog(QWidget* parent,
                          DialogType type,
                          DialogLevel level,
                          const QString& title,
                          const QString& text,
                          DialogButtons buttons,
                          DialogCallback callback = nullptr,
                          DialogButton defaultButton = DialogButton::Ok);
    ~InlineDialog() override;

    // Static convenience — fire-and-forget messages
    static void information(QWidget* parent, const QString& title, const QString& text, DialogCallback cb = nullptr);
    static void warning(QWidget* parent, const QString& title, const QString& text, DialogCallback cb = nullptr);
    static void critical(QWidget* parent, const QString& title, const QString& text, DialogCallback cb = nullptr);
    static void success(QWidget* parent, const QString& title, const QString& text, DialogCallback cb = nullptr);

    // Static convenience — question
    static void question(QWidget* parent, const QString& title, const QString& text,
                         DialogButtons buttons, DialogCallback cb,
                         DialogButton defaultButton = DialogButton::Save);

    // Static convenience — text input
    static void getText(QWidget* parent, const QString& title, const QString& label,
                        const QString& defaultText, DialogCallback cb);

    // Static convenience — item selection
    static void getItem(QWidget* parent, const QString& title, const QString& label,
                        const QStringList& items, int defaultIndex, DialogCallback cb);

    // Static convenience — custom content
    static void showCustom(QWidget* parent, const QString& title, QWidget* content,
                           DialogButtons buttons, DialogCallback cb);

    // Static convenience — file dialogs (non-blocking wrappers)
    static void getOpenFileName(QWidget* parent, const QString& title,
                                const QString& filter, DialogCallback cb);
    static void getSaveFileName(QWidget* parent, const QString& title,
                                const QString& filter, DialogCallback cb);

    // Animation properties
    qreal bubbleScale() const { return bubbleScale_; }
    void setBubbleScale(qreal s);
    qreal scrimOpacity() const { return scrimOpacity_; }
    void setScrimOpacity(qreal o);

signals:
    void finished(const DialogResult& result);

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void buildUi();
    void addButton(DialogButton btn, bool primary);
    QString buttonLabel(DialogButton btn) const;
    QString levelIcon() const;
    QColor levelIconColor() const;

    void animateIn();
    void animateOut();
    void dismiss(DialogButton btn);
    void layoutBubble();

    // Config
    DialogType type_;
    DialogLevel level_;
    QString title_;
    QString text_;
    DialogButtons buttons_;
    DialogCallback callback_;
    DialogButton defaultButton_;

    // Animation state
    qreal bubbleScale_ = 0.0;
    qreal scrimOpacity_ = 0.0;

    // UI
    QWidget* bubbleCard_ = nullptr;
    QLabel* iconLabel_ = nullptr;
    QLabel* titleLabel_ = nullptr;
    QLabel* textLabel_ = nullptr;
    QLineEdit* inputEdit_ = nullptr;
    QComboBox* itemCombo_ = nullptr;
    QWidget* contentWidget_ = nullptr;
    QList<QPushButton*> actionButtons_;
    QGraphicsOpacityEffect* bubbleOpacity_ = nullptr;

    // Animation groups
    QParallelAnimationGroup* showAnim_ = nullptr;
    QParallelAnimationGroup* hideAnim_ = nullptr;

    // Auto-dismiss timer for fire-and-forget
    QTimer* autoDismissTimer_ = nullptr;

    DialogResult result_;
    bool dismissed_ = false;
};

} // namespace acutesim::gui::widgets
