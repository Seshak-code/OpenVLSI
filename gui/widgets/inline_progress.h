#pragma once

#include <QWidget>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QGraphicsOpacityEffect>

namespace acutesim::gui::widgets {

class InlineProgress : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal bubbleScale READ bubbleScale WRITE setBubbleScale)
    Q_PROPERTY(qreal scrimOpacity READ scrimOpacity WRITE setScrimOpacity)

public:
    explicit InlineProgress(QWidget* parent);
    ~InlineProgress() override;

    void setTitle(const QString& title);
    void setRange(int min, int max);
    void setValue(int value);
    void setCancelEnabled(bool enabled);

    void show();
    void dismiss();

    qreal bubbleScale() const { return bubbleScale_; }
    void setBubbleScale(qreal s);
    qreal scrimOpacity() const { return scrimOpacity_; }
    void setScrimOpacity(qreal o);

signals:
    void cancelled();

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void buildUi();
    void layoutBubble();
    void animateIn();
    void animateOut();

    qreal bubbleScale_ = 0.0;
    qreal scrimOpacity_ = 0.0;

    QWidget* bubbleCard_ = nullptr;
    QLabel* titleLabel_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QPushButton* cancelBtn_ = nullptr;
    QGraphicsOpacityEffect* bubbleOpacity_ = nullptr;

    bool dismissed_ = false;
};

} // namespace acutesim::gui::widgets
