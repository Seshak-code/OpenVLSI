#pragma once
// The Test stage: the designed chip under test. A panel lists tests and starts new ones; a run opens as a tab with its verdict,
// its log and every signal it recorded, as digital waveforms.
#include "app/session.h"
#include "flow/vcd.h"

#include <QJsonObject>
#include <QMainWindow>
#include <QWidget>

#include <functional>
#include <vector>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTimer;
class QTreeWidget;

namespace app {

// Digital waveforms: a row per signal, zoom and pan in time, a cursor that reads every signal's value
class DigitalWaves : public QWidget {
    Q_OBJECT
public:
    explicit DigitalWaves(QWidget* parent = nullptr);
    void load(const flow::Vcd& vcd);      // every recorded signal is available; nothing shown yet
    bool show(const QString& signal);     // adds its row; false when no such signal
    void hide(const QString& signal);
    void clear();
    QStringList shown() const;
    void fit();
    void setCursorTime(int64_t t);
    int64_t cursorTime() const { return cursor_; }
    QString valueAtCursor(const QString& signal) const;  // hex; "" when it is not shown
    QSize sizeHint() const override { return {700, 400}; }

signals:
    void cursorMoved(qint64 time);

protected:
    void paintEvent(QPaintEvent*) override;
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void resizeEvent(QResizeEvent*) override;

private:
    struct Row { QString name; int width; const std::vector<flow::VcdChange>* changes; };
    double xOf(int64_t t) const;
    int64_t timeOf(double x) const;
    flow::Vcd vcd_;
    std::vector<Row> rows_;
    int64_t from_ = 0, to_ = 1, cursor_ = -1;
    int nameWidth_ = 220;
};

class TestRunView : public QMainWindow {
    Q_OBJECT
public:
    TestRunView(Session& session, const QString& run, QWidget* parent = nullptr);
    QString run() const { return run_; }
    QString verdict() const;
    QString logText() const;
    DigitalWaves* waves() const { return waves_; }
    QTreeWidget* signalTree() const { return signals_; }
    void refresh();

private:
    void loadWaves(const QString& vcd);
    Session& session_;
    QString run_, loaded_;
    QLabel* title_;
    QLabel* verdict_;
    QLabel* cursorText_;
    QPlainTextEdit* log_;
    QTreeWidget* signals_;
    QLineEdit* filter_;
    DigitalWaves* waves_;
    QTimer* poll_;
};

class TestPanel : public QWidget {
    Q_OBJECT
public:
    struct Actions { std::function<void(const QString& run)> open; };
    TestPanel(Session& session, Actions actions, QWidget* parent = nullptr);
    void refresh();
    QTreeWidget* runs() const { return tree_; }
    void newTest();

private:
    Session& session_;
    Actions actions_;
    QTreeWidget* tree_;
    QTimer* poll_;
};

}  // namespace app
