#pragma once
// The Implement stage: Verilog to GDS. A panel lists the design's runs and starts new ones; a run opens as a tab with its
// pipeline, every stage's metrics, its log and whether it is clean. Everything here reads and starts through flow_* commands.
#include "app/session.h"

#include <QJsonObject>
#include <QMainWindow>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTimer;
class QTreeWidget;

namespace app {

// The stages as a line of nodes: each one's state by colour and shape, its time beneath
class StagePipeline : public QWidget {
    Q_OBJECT
public:
    explicit StagePipeline(QWidget* parent = nullptr);
    void setStages(const QJsonArray& stages);  // from flow_status
    QString stateOf(const QString& stage) const;
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;

private:
    struct Node { QString name, state; double elapsed = -1; };
    std::vector<Node> nodes_;
};

class FlowRunView : public QMainWindow {
    Q_OBJECT
public:
    FlowRunView(Session& session, const QString& run, QWidget* parent = nullptr);
    QString run() const { return run_; }
    QString design() const { return run_.section('/', 0, 0); }
    void refresh();  // status, log and, when anything finished, the report
    StagePipeline* pipeline() const { return pipeline_; }
    QTableWidget* metrics() const { return metrics_; }
    QString verdict() const;
    QString logText() const;
    QString reasons() const;

signals:
    void openLayoutRequested(const QString& run);
    void showMarkersRequested(const QString& run);
    void signoffRequested(const QString& run, bool timing);

private:
    void fillReport(const QJsonObject& report);
    Session& session_;
    QString run_, lastFingerprint_;
    StagePipeline* pipeline_;
    QTableWidget* metrics_;
    QPlainTextEdit* log_;
    QLabel* title_;
    QLabel* verdict_;
    QLabel* reasons_;
    QPushButton* stop_;
    QPushButton* layout_;
    QPushButton* markers_;
    QPushButton* timing_;
    QPushButton* power_;
    QTimer* poll_;
};

class ImplementPanel : public QWidget {
    Q_OBJECT
public:
    struct Actions { std::function<void(const QString& run)> open; };
    ImplementPanel(Session& session, Actions actions, QWidget* parent = nullptr);
    void refresh();
    QTreeWidget* runs() const { return tree_; }
    QString toolsText() const;
    void newRun();

private:
    Session& session_;
    Actions actions_;
    QTreeWidget* tree_;
    QLabel* tools_;
    QTimer* poll_;
};

}  // namespace app
