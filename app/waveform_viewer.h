#pragma once
// The waveform viewer (ViVA) on one run: traces from calculator expressions over the run's results, cursors A and B
// with each trace's values and their difference, and a calculator line (a number, or a waveform to plot). It
// cross-probes: selecting a trace of one net (VT(out), VF(out), ...) selects that net in the run's schematic, and a
// schematic's Plot Net adds a trace here. The CIW's waveform_* commands drive it; this window does nothing they cannot.
#include "app/plot.h"
#include "app/session.h"
#include "sim/run.h"

#include <QMainWindow>

class QLabel;
class QLineEdit;
class QListWidget;
class QTableWidget;

namespace app {

class WaveformViewer : public QMainWindow {
    Q_OBJECT
public:
    WaveformViewer(Session& session, const QString& run, QWidget* parent = nullptr);  // run: a checked run directory
    QString run() const { return run_; }
    QString design() const { return design_; }  // "lib/cell/view": the run's top schematic ("" when unknown)
    QString defaultAccess() const;  // a probed net's: VT, VF or VDC as the plot's scale is, else the run's first analysis

    bool add(const QString& expression, QString* error = nullptr);  // a trace (a waveform; a number is an error)
    void clear();
    QStringList expressions() const;
    QString calculate(const QString& expression);  // the calculator line: a number's text, or plots a waveform
    void selectTrace(int index);                    // and cross-probes its net
    Plot* plot() const { return plot_; }
    QTableWidget* readout() const { return readout_; }  // x, then each trace: at A, at B, B − A
    QString status() const;

private:
    void redraw();
    void updateReadout();

    Session& session_;
    QString run_, design_;
    std::vector<sim::Plot> results_;
    std::vector<std::pair<QString, PlotTrace>> traces_;  // expression, trace
    QString scale_;                                      // the traces' x (one per plot)
    Plot* plot_;
    QListWidget* list_;
    QTableWidget* readout_;
    QLineEdit* calc_;
    QLabel* status_;
};

}  // namespace app
