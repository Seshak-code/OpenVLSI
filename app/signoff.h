#pragma once
// Sign-off over an implementation run: timing paths, and the power grid's IR drop. Each opens as a tab and shows what it finds on the layout.
#include "app/session.h"

#include <QJsonObject>
#include <QMainWindow>

class QLabel;
class QPushButton;
class QTableWidget;
class QTimer;

namespace app {

class SignoffView : public QMainWindow {
    Q_OBJECT
public:
    enum class Kind { Timing, Power };
    SignoffView(Session& session, const QString& run, Kind kind, QWidget* parent = nullptr);
    QString run() const { return run_; }
    Kind kind() const { return kind_; }
    QString summary() const;
    QTableWidget* list() const { return list_; }
    QTableWidget* detail() const { return detail_; }
    void refresh();
    void selectRow(int row);
    bool showOnLayout();  // the selected path, or the IR map
    bool setup();         // timing: a form for the modes, corners and RC derates, then the analysis

private:
    Session& session_;
    QString run_;
    Kind kind_;
    QLabel* title_;
    QLabel* summary_;
    QPushButton* analyze_;
    QPushButton* show_;
    QTableWidget* list_;
    QTableWidget* detail_;
    QTimer* poll_;
    QJsonObject report_;
    QString mode_ = "setup";
};

}  // namespace app
