#include "app/waveform_viewer.h"

#include "sim/calc.h"

#include <QDockWidget>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QStatusBar>
#include <QTableWidget>
#include <QToolBar>
#include <QVBoxLayout>

#include <filesystem>
#include <fstream>

namespace app {

namespace {
const QColor kColors[] = {{220, 60, 60}, {40, 110, 230}, {30, 160, 70}, {200, 120, 20}, {150, 60, 200}, {20, 160, 170}, {120, 120, 120}};
QString number(std::complex<double> v) {
    if (v.imag() == 0) return si(v.real(), 6);
    return si(v.real(), 6) + (v.imag() < 0 ? " - j" : " + j") + si(std::abs(v.imag()), 6);
}
}  // namespace

WaveformViewer::WaveformViewer(Session& session, const QString& run, QWidget* parent)
    : QMainWindow(parent, Qt::Window), session_(session), run_(run), plot_(new Plot(this)), list_(new QListWidget(this)),
      readout_(new QTableWidget(0, 4, this)), calc_(new QLineEdit(this)), status_(new QLabel(this)), derivation_(new QPlainTextEdit(this)) {
    namespace fs = std::filesystem;
    setAttribute(Qt::WA_DeleteOnClose);
    results_ = sim::readRaw((fs::path(run.toStdString()) / "out.raw").string());
    std::ifstream deck(fs::path(run.toStdString()) / "input.cir");  // its title: "* lib/cell/view (OpenVLSI)"
    std::string title;
    std::getline(deck, title);
    static const QRegularExpression top(R"(^\* (\S+/\S+/\S+) \(OpenVLSI\)$)");
    if (const auto m = top.match(QString::fromStdString(title)); m.hasMatch()) design_ = m.captured(1);
    setWindowTitle("Waveforms: " + (design_.isEmpty() ? QString() : design_ + " ") + QFileInfo(run).fileName());

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->addWidget(plot_, 1);
    auto* calcRow = new QHBoxLayout;
    calcRow->addWidget(new QLabel("Calculator", this));
    calc_->setPlaceholderText("riseTime(VT(out)), dB20(VF(out)), bandwidth(VF(out)), ...");
    calcRow->addWidget(calc_, 1);
    layout->addLayout(calcRow);
    derivation_->setReadOnly(true);
    derivation_->setFont(QFont("Menlo"));
    derivation_->setMaximumHeight(120);
    derivation_->setPlaceholderText("The calculator's steps: each vector read, each function and how it got its value.");
    layout->addWidget(derivation_);
    readout_->setHorizontalHeaderLabels({"Trace", "A", "B", "B − A"});
    readout_->horizontalHeader()->setStretchLastSection(true);
    readout_->verticalHeader()->hide();
    readout_->setMaximumHeight(150);
    layout->addWidget(readout_);
    setCentralWidget(central);
    auto* dock = new QDockWidget("Traces", this);
    list_->setMaximumWidth(260);
    dock->setWidget(list_);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
    statusBar()->addWidget(status_, 1);
    QToolBar* bar = addToolBar("Waveforms");
    bar->addAction("Remove Trace", this, [this] {
        const int k = list_->currentRow();
        if (k < 0) return;
        traces_.erase(traces_.begin() + k);
        if (traces_.empty()) scale_.clear();
        redraw();
    });
    bar->addAction("Clear", this, &WaveformViewer::clear);
    bar->addAction("Fit", plot_, &Plot::fit);

    connect(calc_, &QLineEdit::returnPressed, this, [this] { calculate(calc_->text()); });
    connect(plot_, &Plot::cursorsChanged, this, &WaveformViewer::updateReadout);
    connect(list_, &QListWidget::currentRowChanged, this, [this](int k) {
        if (k < 0 || k >= (int)traces_.size() || design_.isEmpty()) return;
        // a trace of one net cross-probes: the net is selected in the schematic
        static const QRegularExpression net(R"re(^\s*(VT|VF|VDC|VOP)\(\s*"?([^")\s]+)"?\s*\)\s*$)re");
        const auto m = net.match(traces_[(size_t)k].first);
        if (!m.hasMatch()) return;
        const QStringList d = design_.split('/');
        const cmd::Result r = session_.call("editor_select_net", {{"library", d[0]}, {"cell", d[1]}, {"view", d[2]}, {"net", m.captured(2)}});
        status_->setText(r.ok ? "Selected " + m.captured(2) + " in " + design_ : "✗ " + r.error);
    });
    resize(1000, 650);
}

QString WaveformViewer::status() const { return status_->text(); }

QString WaveformViewer::defaultAccess() const {
    // the first analysis the run has whose scale the plot already shows (any scale when it is empty); a sweep's
    // scale (v-sweep, i-sweep, temp) is the dc's
    struct A { const char *plot, *access, *scale; };
    static const A order[] = {{"Transient Analysis", "VT", "time"}, {"Periodic Steady State", "VPSS", "time"}, {"AC Analysis", "VF", "frequency"},
                              {"PSS Harmonics", "VH", "frequency"}, {"DC transfer characteristic", "VDC", ""}};
    const bool sweep = !scale_.isEmpty() && scale_ != "time" && scale_ != "frequency";
    for (const A& a : order)
        if (scale_.isEmpty() || scale_ == a.scale || (sweep && !*a.scale))
            for (const auto& p : results_) if (p.name == a.plot) return a.access;
    return {};
}

QStringList WaveformViewer::expressions() const {
    QStringList out;
    for (const auto& t : traces_) out << t.first;
    return out;
}

bool WaveformViewer::add(const QString& expression, QString* error) {
    auto fail = [&](const QString& e) {
        status_->setText("✗ " + e);
        if (error) *error = e;
        return false;
    };
    sim::Wave w;
    try {
        w = sim::evaluate(expression.toStdString(), sim::resultsLookup(results_));
    } catch (const std::exception& e) {
        return fail(QString::fromUtf8(e.what()));
    }
    if (w.scalar()) return fail(expression + " is a number (" + number(w.y[0]) + "), not a waveform to plot");
    const QString scale = QString::fromStdString(w.scale);
    if (!scale_.isEmpty() && scale != scale_) return fail(expression + " is over " + scale + "; this plot shows " + scale_);
    PlotTrace t{expression, kColors[traces_.size() % std::size(kColors)], w.x, {}};
    bool complex = false;
    for (const auto& v : w.y) complex = complex || v.imag() != 0;
    for (const auto& v : w.y) t.y.push_back(complex ? std::abs(v) : v.real());
    if (complex) t.name = "|" + expression + "|";  // a complex waveform plots its magnitude
    scale_ = scale;
    traces_.emplace_back(expression, t);
    redraw();
    status_->setText("Plotted " + t.name);
    return true;
}

void WaveformViewer::clear() {
    traces_.clear();
    scale_.clear();
    redraw();
}

QString WaveformViewer::derivation() const { return derivation_->toPlainText(); }

QString WaveformViewer::calculate(const QString& expression) {
    std::vector<std::string> steps;
    auto show = [&] {
        QStringList lines;
        for (const auto& s : steps) lines << QString::fromStdString(s);
        derivation_->setPlainText(lines.join('\n'));
    };
    try {
        const sim::Wave w = sim::evaluate(expression.toStdString(), sim::resultsLookup(results_), &steps);
        show();
        if (!w.scalar()) return add(expression) ? "plotted" : status_->text();
        status_->setText(expression + " = " + number(w.y[0]));
        return number(w.y[0]);
    } catch (const std::exception& e) {
        show();  // as far as it got
        status_->setText("✗ " + QString::fromUtf8(e.what()));
        return status_->text();
    }
}

void WaveformViewer::selectTrace(int index) { list_->setCurrentRow(index); }

void WaveformViewer::redraw() {
    std::vector<PlotTrace> ts;
    list_->blockSignals(true);
    list_->clear();
    for (const auto& [e, t] : traces_) {
        ts.push_back(t);
        auto* item = new QListWidgetItem(t.name, list_);
        item->setForeground(t.color);
    }
    list_->blockSignals(false);
    plot_->setTraces(std::move(ts), scale_ == "frequency", scale_);
    updateReadout();
}

void WaveformViewer::updateReadout() {
    readout_->setRowCount((int)traces_.size() + 1);
    const auto a = plot_->cursor(0), b = plot_->cursor(1);
    auto cellText = [](std::optional<double> v) { return v ? si(*v, 6) : QString("—"); };
    auto row = [&](int r, const QString& name, std::optional<double> va, std::optional<double> vb) {
        readout_->setItem(r, 0, new QTableWidgetItem(name));
        readout_->setItem(r, 1, new QTableWidgetItem(cellText(va)));
        readout_->setItem(r, 2, new QTableWidgetItem(cellText(vb)));
        readout_->setItem(r, 3, new QTableWidgetItem(va && vb ? si(*vb - *va, 6) : QString("—")));
    };
    row(0, scale_.isEmpty() ? "x" : scale_, a, b);
    for (size_t k = 0; k < traces_.size(); ++k) {
        const PlotTrace& t = plot_->traces()[k];
        row((int)k + 1, t.name, a ? plot_->valueAt(t, *a) : std::nullopt, b ? plot_->valueAt(t, *b) : std::nullopt);
    }
}

}  // namespace app
