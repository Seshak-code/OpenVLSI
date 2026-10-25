#pragma once
// ADE Explorer (Cadence's) on a maestro view: the setup's tests, variables and corners in tables, the outputs with
// their specs and each run's results beside them. It works only through commands: an edit is ade_setup_set (one undo
// step; refused, it reverts), Run is ade_run in the background, the results are ade_history_get polled until done.
//
// The tables' text forms: an analysis list "op; tran step=1n stop=100n"; models "models.lib:tt; other.lib" (file,
// then its section); variables, options "reltol=1e-4 gmin=1e-12". A blank last row adds; clearing a name removes.
#include "app/session.h"

#include <QJsonArray>
#include <QMainWindow>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTableWidget;
class QTimer;

namespace app {

class AdeExplorer : public QMainWindow {
    Q_OBJECT
public:
    AdeExplorer(Session& session, const QString& lib, const QString& cell, const QString& view, QWidget* parent = nullptr);

    QTableWidget* tests() const { return tests_; }        // On, Name, Design, Analyses, Models, Options
    QTableWidget* variables() const { return vars_; }     // Name, Value
    QTableWidget* corners() const { return corners_; }    // On, Name, Temperature, Variables, Models
    QTableWidget* outputs() const { return outputs_; }    // Test, Name, Expression, Min, Max, then one column per point
    QString status() const;
    bool running() const { return !running_.isEmpty(); }
    QStringList histories() const;
    bool openResult(int row, int column);  // a result's point in the waveform viewer, its waveform plotted (a double-click)
    // a result's derivation: the output's equation and value, every parameter its point ran with, the calculator's
    // steps, the spec's verdict (the Derivation pane shows the selected result's)
    QString derivation(int row, int column) const;

public slots:
    void run();
    void stop();
    void load();  // the setup, from the database

private:
    QJsonObject collect(QString& error) const;  // the setup the tables show
    void commit();
    void poll();
    void showHistory(const QString& name);
    QJsonObject args() const;

    Session& session_;
    QString lib_, cell_, view_;
    QTableWidget *tests_, *vars_, *corners_, *outputs_;
    QLineEdit* temperature_;
    QCheckBox* nominal_;
    QComboBox* history_;
    QLabel* status_;
    QPlainTextEdit* derivation_;
    QTimer* poll_;
    QString running_;
    QJsonArray shownPoints_;  // the history the outputs table shows
    bool loading_ = false;
};

}  // namespace app
