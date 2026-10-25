#include "app/ade_explorer.h"

#include "sim/calc.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

namespace app {

namespace {

QTableWidget* table(const QStringList& headers, QWidget* parent) {
    auto* t = new QTableWidget(0, (int)headers.size(), parent);
    t->setHorizontalHeaderLabels(headers);
    t->horizontalHeader()->setStretchLastSection(true);
    t->verticalHeader()->hide();
    return t;
}
QString text(const QTableWidget* t, int row, int col) { return t->item(row, col) ? t->item(row, col)->text().trimmed() : QString(); }
bool on(const QTableWidget* t, int row) { return !t->item(row, 0) || t->item(row, 0)->checkState() == Qt::Checked; }
void put(QTableWidget* t, int row, int col, const QString& v) { t->setItem(row, col, new QTableWidgetItem(v)); }
void check(QTableWidget* t, int row, bool v) {
    auto* i = new QTableWidgetItem;
    i->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    i->setCheckState(v ? Qt::Checked : Qt::Unchecked);
    t->setItem(row, 0, i);
}

// "k=v k2=v2"
QString pairs(const QJsonObject& o) {
    QStringList out;
    for (auto it = o.begin(); it != o.end(); ++it) out << it.key() + "=" + it.value().toString();
    return out.join(' ');
}
QJsonObject parsePairs(const QString& t, QString& error) {
    QJsonObject o;
    for (const QString& w : t.split(' ', Qt::SkipEmptyParts)) {
        const qsizetype eq = w.indexOf('=');
        if (eq <= 0 || eq == w.size() - 1) error = "'" + w + "' is not name=value";
        else o[w.left(eq)] = w.mid(eq + 1);
    }
    return o;
}
// "op; tran step=1n stop=100n"
QString analysesText(const QJsonArray& as) {
    QStringList out;
    for (const auto& v : as) {
        QJsonObject a = v.toObject();
        const QString type = a.take("type").toString();
        out << (a.isEmpty() ? type : type + " " + pairs(a));
    }
    return out.join("; ");
}
QJsonArray parseAnalyses(const QString& t, QString& error) {
    QJsonArray out;
    for (const QString& part : t.split(';', Qt::SkipEmptyParts)) {
        const QString p = part.trimmed();
        if (p.isEmpty()) continue;
        const qsizetype sp = p.indexOf(' ');
        QJsonObject a = sp < 0 ? QJsonObject() : parsePairs(p.mid(sp + 1), error);
        a["type"] = sp < 0 ? p : p.left(sp);
        out.append(a);
    }
    return out;
}
// "models.lib:tt; other.lib"
QString modelsText(const QJsonArray& ms) {
    QStringList out;
    for (const auto& v : ms) {
        const QJsonObject m = v.toObject();
        out << (m["section"].toString().isEmpty() ? m["file"].toString() : m["file"].toString() + ":" + m["section"].toString());
    }
    return out.join("; ");
}
QJsonArray parseModels(const QString& t) {
    QJsonArray out;
    for (const QString& part : t.split(';', Qt::SkipEmptyParts)) {
        const QString p = part.trimmed();
        if (p.isEmpty()) continue;
        const qsizetype colon = p.lastIndexOf(':');
        out.append(colon < 0 ? QJsonObject{{"file", p}} : QJsonObject{{"file", p.left(colon)}, {"section", p.mid(colon + 1)}});
    }
    return out;
}

QString shown(const QJsonValue& v) {
    if (v.isDouble()) return QString::number(v.toDouble(), 'g', 6);
    const QJsonObject o = v.toObject();
    if (o.contains("wave")) return QString("wave (%1 points)").arg(o["wave"].toInt());
    return QString("%1 %2 j%3").arg(o["re"].toDouble(), 0, 'g', 6).arg(o["im"].toDouble() < 0 ? "-" : "+").arg(std::abs(o["im"].toDouble()), 0, 'g', 6);
}

}  // namespace

AdeExplorer::AdeExplorer(Session& session, const QString& lib, const QString& cell, const QString& view, QWidget* parent)
    : QMainWindow(parent, Qt::Window), session_(session), lib_(lib), cell_(cell), view_(view), poll_(new QTimer(this)) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("ADE Explorer Editing: " + lib + " " + cell + " " + view);
    tests_ = table({"On", "Name", "Design (lib/cell/view)", "Analyses", "Models (file:section)", "Options"}, this);
    vars_ = table({"Name", "Value (a list sweeps)"}, this);
    corners_ = table({"On", "Name", "Temperature", "Variables", "Models (file:section)"}, this);
    outputs_ = table({"Test", "Name", "Expression", "Min", "Max"}, this);
    mc_ = table({"Samples", "Seed", "Temperature", "Variables", "Models (file:section)"}, this);
    mc_->setRowCount(1);
    temperature_ = new QLineEdit(this);
    nominal_ = new QCheckBox("Run the nominal corner", this);

    auto* setup = new QTabWidget(this);
    setup->addTab(tests_, "Tests");
    auto* varPage = new QWidget(this);
    auto* varLayout = new QFormLayout(varPage);
    varLayout->addRow("Temperature", temperature_);
    varLayout->addRow(vars_);
    setup->addTab(varPage, "Variables");
    auto* cornerPage = new QWidget(this);
    auto* cornerLayout = new QVBoxLayout(cornerPage);
    cornerLayout->addWidget(nominal_);
    cornerLayout->addWidget(corners_);
    setup->addTab(cornerPage, "Corners");
    setup->addTab(mc_, "Monte Carlo");
    derivation_ = new QPlainTextEdit(this);
    derivation_->setReadOnly(true);
    derivation_->setFont(QFont("Menlo"));
    derivation_->setPlaceholderText("Select a result to see how it was computed: its equation, the parameters its point ran with, each step.");
    auto* split = new QSplitter(Qt::Vertical, this);
    split->addWidget(setup);
    split->addWidget(outputs_);
    split->addWidget(derivation_);
    setCentralWidget(split);

    QToolBar* bar = addToolBar("ADE");
    bar->addAction("Run", this, [this] { run(); });
    bar->addAction("Run Monte Carlo", this, [this] { run("montecarlo"); });
    bar->addAction("Stop", this, &AdeExplorer::stop);
    bar->addSeparator();
    bar->addWidget(new QLabel(" History ", this));
    history_ = new QComboBox(this);
    history_->setMinimumContentsLength(14);
    bar->addWidget(history_);
    status_ = new QLabel(this);
    statusBar()->addWidget(status_, 1);

    for (QTableWidget* t : {tests_, vars_, corners_, outputs_, mc_})
        connect(t, &QTableWidget::itemChanged, this, [this] { if (!loading_) commit(); });
    connect(temperature_, &QLineEdit::editingFinished, this, [this] { if (!loading_) commit(); });
    connect(nominal_, &QCheckBox::toggled, this, [this] { if (!loading_) commit(); });
    connect(history_, &QComboBox::currentTextChanged, this, [this](const QString& h) { if (!loading_) showHistory(h); });
    connect(poll_, &QTimer::timeout, this, &AdeExplorer::poll);
    connect(outputs_, &QTableWidget::cellDoubleClicked, this, [this](int row, int col) { openResult(row, col); });
    connect(outputs_, &QTableWidget::currentCellChanged, this, [this](int row, int col) { derivation_->setPlainText(derivation(row, col)); });
    connect(&session_, &Session::changed, this, [this] { if (!loading_) load(); });
    load();
    resize(1100, 700);
}

QJsonObject AdeExplorer::args() const { return {{"library", lib_}, {"cell", cell_}, {"view", view_}}; }
QString AdeExplorer::status() const { return status_->text(); }

QStringList AdeExplorer::histories() const {
    QStringList out;
    for (int k = 0; k < history_->count(); ++k) out << history_->itemText(k);
    return out;
}

void AdeExplorer::load() {
    const cmd::Result r = session_.call("ade_setup_get", args());
    if (!r.ok) {
        status_->setText("✗ " + r.error);
        return;
    }
    const QJsonObject s = r.value.toObject();
    loading_ = true;
    const QJsonArray ts = s["tests"].toArray(), cs = s["corners"].toArray(), os = s["outputs"].toArray();
    const QJsonObject vs = s["variables"].toObject();
    tests_->setRowCount(0);
    tests_->setRowCount((int)ts.size() + 1);
    for (int k = 0; k <= ts.size(); ++k) {
        const QJsonObject t = k < ts.size() ? ts[k].toObject() : QJsonObject();
        check(tests_, k, t["enabled"].toBool(true));
        put(tests_, k, 1, t["name"].toString());
        put(tests_, k, 2, t["design"].toString());
        put(tests_, k, 3, analysesText(t["analyses"].toArray()));
        put(tests_, k, 4, modelsText(t["models"].toArray()));
        put(tests_, k, 5, pairs(t["options"].toObject()));
    }
    vars_->setRowCount(0);
    vars_->setRowCount((int)vs.size() + 1);
    int row = 0;
    for (auto it = vs.begin(); it != vs.end(); ++it, ++row) put(vars_, row, 0, it.key()), put(vars_, row, 1, it.value().toString());
    corners_->setRowCount(0);
    corners_->setRowCount((int)cs.size() + 1);
    for (int k = 0; k <= cs.size(); ++k) {
        const QJsonObject c = k < cs.size() ? cs[k].toObject() : QJsonObject();
        check(corners_, k, c["enabled"].toBool(true));
        put(corners_, k, 1, c["name"].toString());
        put(corners_, k, 2, c["temperature"].toString());
        put(corners_, k, 3, pairs(c["variables"].toObject()));
        put(corners_, k, 4, modelsText(c["models"].toArray()));
    }
    outputs_->setRowCount(0);
    outputs_->setRowCount((int)os.size() + 1);
    for (int k = 0; k < os.size(); ++k) {
        const QJsonObject o = os[k].toObject();
        int c = 0;
        for (const char* key : {"test", "name", "expression", "min", "max"}) put(outputs_, k, c++, o[key].toString());
    }
    const QJsonObject mcs = s["montecarlo"].toObject();
    put(mc_, 0, 0, mcs.contains("samples") ? QString::number(mcs["samples"].toInt()) : QString());
    put(mc_, 0, 1, mcs.contains("seed") ? QString::number(mcs["seed"].toInt()) : QString());
    put(mc_, 0, 2, mcs["temperature"].toString());
    put(mc_, 0, 3, pairs(mcs["variables"].toObject()));
    put(mc_, 0, 4, modelsText(mcs["models"].toArray()));
    temperature_->setText(s["temperature"].toString());
    nominal_->setChecked(s["nominal"].toBool(true));
    for (QTableWidget* t : {tests_, vars_, corners_, mc_}) t->resizeColumnsToContents();
    const QString current = history_->currentText();
    history_->clear();
    for (const auto& h : session_.call("ade_history_list", args()).value.toArray()) history_->addItem(h.toString());
    history_->setCurrentText(current.isEmpty() ? history_->itemText(history_->count() - 1) : current);
    loading_ = false;
    showHistory(history_->currentText());
}

QJsonObject AdeExplorer::collect(QString& error) const {
    QJsonArray tests, corners, outputs;
    for (int k = 0; k < tests_->rowCount(); ++k) {
        if (text(tests_, k, 1).isEmpty()) continue;
        QJsonObject t{{"name", text(tests_, k, 1)}, {"design", text(tests_, k, 2)}, {"enabled", on(tests_, k)},
                      {"analyses", parseAnalyses(text(tests_, k, 3), error)}};
        if (const QJsonArray m = parseModels(text(tests_, k, 4)); !m.isEmpty()) t["models"] = m;
        if (const QJsonObject o = parsePairs(text(tests_, k, 5), error); !o.isEmpty()) t["options"] = o;
        tests.append(t);
    }
    QJsonObject vars;
    for (int k = 0; k < vars_->rowCount(); ++k)
        if (!text(vars_, k, 0).isEmpty()) vars[text(vars_, k, 0)] = text(vars_, k, 1);
    for (int k = 0; k < corners_->rowCount(); ++k) {
        if (text(corners_, k, 1).isEmpty()) continue;
        QJsonObject c{{"name", text(corners_, k, 1)}, {"enabled", on(corners_, k)}};
        if (!text(corners_, k, 2).isEmpty()) c["temperature"] = text(corners_, k, 2);
        if (const QJsonObject v = parsePairs(text(corners_, k, 3), error); !v.isEmpty()) c["variables"] = v;
        if (const QJsonArray m = parseModels(text(corners_, k, 4)); !m.isEmpty()) c["models"] = m;
        corners.append(c);
    }
    for (int k = 0; k < outputs_->rowCount(); ++k) {
        if (text(outputs_, k, 1).isEmpty()) continue;
        QJsonObject o{{"test", text(outputs_, k, 0)}, {"name", text(outputs_, k, 1)}, {"expression", text(outputs_, k, 2)}};
        if (!text(outputs_, k, 3).isEmpty()) o["min"] = text(outputs_, k, 3);
        if (!text(outputs_, k, 4).isEmpty()) o["max"] = text(outputs_, k, 4);
        outputs.append(o);
    }
    QJsonObject s{{"tests", tests}, {"variables", vars}, {"corners", corners}, {"outputs", outputs}, {"nominal", nominal_->isChecked()}};
    if (!text(mc_, 0, 0).isEmpty()) {  // a Monte Carlo section once it has a sample count
        bool ok = false;
        QJsonObject mc{{"samples", text(mc_, 0, 0).toInt(&ok)}};
        if (!ok) error = "Monte Carlo samples: '" + text(mc_, 0, 0) + "' is not a whole number";
        if (!text(mc_, 0, 1).isEmpty()) {
            mc["seed"] = text(mc_, 0, 1).toInt(&ok);
            if (!ok) error = "Monte Carlo seed: '" + text(mc_, 0, 1) + "' is not a whole number";
        }
        if (!text(mc_, 0, 2).isEmpty()) mc["temperature"] = text(mc_, 0, 2);
        if (const QJsonObject v = parsePairs(text(mc_, 0, 3), error); !v.isEmpty()) mc["variables"] = v;
        if (const QJsonArray m = parseModels(text(mc_, 0, 4)); !m.isEmpty()) mc["models"] = m;
        s["montecarlo"] = mc;
    }
    if (!temperature_->text().trimmed().isEmpty()) s["temperature"] = temperature_->text().trimmed();
    return s;
}

void AdeExplorer::commit() {
    bool mcTyped = false;
    for (int c = 1; c < mc_->columnCount(); ++c) mcTyped = mcTyped || !text(mc_, 0, c).isEmpty();
    if (mcTyped && text(mc_, 0, 0).isEmpty()) {  // a Monte Carlo row in progress: kept as typed until it has its samples
        status_->setText("Monte Carlo: enter the number of samples to save it");
        return;
    }
    QString error;
    const QJsonObject setup = collect(error);
    cmd::Result r;
    if (error.isEmpty()) {
        QJsonObject a = args();
        a["setup"] = setup;
        r = session_.call("ade_setup_set", a);
        error = r.error;
    }
    if (!r.ok) {
        status_->setText("✗ " + error);
        load();  // the tables go back to what the database holds
        status_->setText("✗ " + error);
    } else {
        status_->setText("Setup saved");
    }
}

QString AdeExplorer::derivation(int row, int column) const {
    if (shownMode_ == "montecarlo") {  // how each statistic came about
        const QJsonObject st = shownStats_[text(outputs_, row, 1)].toObject();
        if (row < 0 || column < 5 || st.isEmpty()) return {};
        int first = 0, last = 0;
        QStringList params;
        for (const auto& pv : shownPoints_) {
            const QJsonObject pt = pv.toObject();
            if (pt["test"].toString() != text(outputs_, row, 0)) continue;
            if (!first) {
                first = pt["seed"].toInt();
                QStringList models;
                for (const auto& m : pt["models"].toArray()) models << m.toString();
                QStringList vars;
                const QJsonObject v = pt["variables"].toObject();
                for (auto it = v.begin(); it != v.end(); ++it) vars << it.key() + " = " + it.value().toString();
                params << "  design       " + pt["design"].toString() << "  temperature  " + pt["temperature"].toString() + " °C"
                       << "  variables    " + (vars.isEmpty() ? "none" : vars.join(", ")) << "  models       " + (models.isEmpty() ? "none" : models.join("\n               "))
                       << "  analyses     " + pt["analyses"].toString().replace('\n', "; ");
            }
            last = pt["seed"].toInt();
        }
        QStringList out;
        out << text(outputs_, row, 1) + " — Monte Carlo over " + QString::number(st["n"].toInt()) + " samples (seeds " + QString::number(first) + " .. " +
                   QString::number(last) + "; " + QString::number(st["errors"].toInt()) + " without a value)";
        out << "Expression  " + text(outputs_, row, 2) << "" << "Parameters (each sample: .options seed = its seed; the PDK's statistics draw)" << params << "";
        out << "Statistics";
        out << "  mean = Σx / n                   = " + QString::fromStdString(sim::si(st["mean"].toDouble()));
        out << "  σ    = √(Σ(x − mean)² / (n − 1)) = " + QString::fromStdString(sim::si(st["sd"].toDouble()));
        out << "  min  = " + QString::fromStdString(sim::si(st["min"].toDouble())) + ", max = " + QString::fromStdString(sim::si(st["max"].toDouble()));
        if (st.contains("yield"))
            out << "  yield = pass / (pass + fail) = " + QString::number(st["pass"].toInt()) + " / " + QString::number(st["pass"].toInt() + st["fail"].toInt()) + " = " +
                       QString::number(st["yield"].toDouble() * 100, 'g', 4) + "%   (spec " + (text(outputs_, row, 3).isEmpty() ? "" : text(outputs_, row, 3) + " <= ") +
                       "x" + (text(outputs_, row, 4).isEmpty() ? "" : " <= " + text(outputs_, row, 4)) + ")";
        out << "" << "Each sample's own derivation is in its point's history (ade_history_get).";
        return out.join('\n');
    }
    const int p = column - 5;
    if (row < 0 || p < 0 || p >= shownPoints_.size() || text(outputs_, row, 0) != shownPoints_[p].toObject()["test"].toString()) return {};
    const QJsonObject pt = shownPoints_[p].toObject(), o = pt["outputs"].toObject()[text(outputs_, row, 1)].toObject();
    QStringList out;
    out << text(outputs_, row, 1) + " — test " + pt["test"].toString() + ", point " + QString::number(pt["index"].toInt()) + " (" + pt["status"].toString() + ")";
    out << "Equation    " + (o.contains("equation") ? o["equation"].toString() : text(outputs_, row, 2));
    out << "";
    out << "Parameters";
    out << "  design       " + pt["design"].toString();
    out << "  corner       " + pt["corner"].toString() + ", temperature " + pt["temperature"].toString() + " °C";
    QStringList vars;
    const QJsonObject v = pt["variables"].toObject();
    for (auto it = v.begin(); it != v.end(); ++it) vars << it.key() + " = " + it.value().toString();
    out << "  variables    " + (vars.isEmpty() ? "none" : vars.join(", "));
    QStringList models;
    for (const auto& m : pt["models"].toArray()) models << m.toString();
    out << "  models       " + (models.isEmpty() ? "none" : models.join("\n               "));
    QStringList opts;
    const QJsonObject op = pt["options"].toObject();
    for (auto it = op.begin(); it != op.end(); ++it) opts << it.key() + "=" + it.value().toString();
    out << "  options      " + (opts.isEmpty() ? "none" : opts.join(" "));
    out << "  analyses     " + pt["analyses"].toString().replace('\n', "; ");
    out << "  netlist      " + pt["dir"].toString() + "/input.cir";
    out << "";
    if (o.contains("error")) out << "Error       " + o["error"].toString();
    else if (pt["status"] != "done") out << "Status      " + pt["status"].toString() + (pt["error"].toString().isEmpty() ? "" : ": " + pt["error"].toString());
    if (!o["steps"].toArray().isEmpty()) {
        out << "Steps";
        int n = 0;
        for (const auto& s : o["steps"].toArray())
            out << (s.toString().startsWith("  ") ? "      " + s.toString().trimmed() : QString("  %1. ").arg(++n) + s.toString());
    }
    if (o.contains("spec")) out << "" << "Spec        " + o["spec"].toString();
    return out.join('\n');
}

bool AdeExplorer::openResult(int row, int column) {
    if (shownMode_ == "montecarlo") return false;  // a statistic has no single point to open
    const int p = column - 5;
    if (p < 0 || p >= shownPoints_.size()) return false;
    const QJsonObject pt = shownPoints_[p].toObject();
    if (pt["status"] != "done") return false;
    cmd::Result r = session_.call("waveform_open", {{"run", pt["dir"]}});
    const QJsonValue value = pt["outputs"].toObject()[text(outputs_, row, 1)].toObject()["value"];
    if (r.ok && value.toObject().contains("wave")) r = session_.call("waveform_add", {{"run", pt["dir"]}, {"expression", text(outputs_, row, 2)}});
    status_->setText(r.ok ? "Opened " + pt["dir"].toString() : "✗ " + r.error);
    return r.ok;
}

void AdeExplorer::run(const QString& mode) {
    if (running()) return;
    QJsonObject a = args();
    a["wait"] = false;
    a["mode"] = mode;
    const cmd::Result r = session_.call("ade_run", a);
    if (!r.ok) {
        status_->setText("✗ " + r.error);
        return;
    }
    running_ = r.value.toObject()["name"].toString();
    loading_ = true;
    history_->addItem(running_);
    history_->setCurrentText(running_);
    loading_ = false;
    status_->setText("Running " + running_);
    poll_->start(300);
    poll();
}

void AdeExplorer::stop() {
    if (!running()) return;
    QJsonObject a = args();
    a["history"] = running_;
    session_.call("ade_stop", a);
}

void AdeExplorer::poll() {
    if (!running()) return poll_->stop();
    showHistory(running_);
    QJsonObject a = args();
    a["history"] = running_;
    const QJsonObject h = session_.call("ade_history_get", a).value.toObject();
    if (h["status"] != "running") {
        poll_->stop();
        const QString done = running_;
        running_.clear();
        showHistory(done);  // as finished: a Monte Carlo run's statistics exist only now
        status_->setText(done + ": " + h["status"].toString());
    }
}

void AdeExplorer::showHistory(const QString& name) {
    outputs_->setColumnCount(5);
    if (name.isEmpty()) return;
    QJsonObject a = args();
    a["history"] = name;
    const cmd::Result r = session_.call("ade_history_get", a);
    if (!r.ok) return;
    const QJsonArray points = r.value.toObject()["points"].toArray();
    shownPoints_ = points;
    shownMode_ = r.value.toObject()["mode"].toString();
    shownStats_ = r.value.toObject()["statistics"].toObject();
    if (shownMode_ == "montecarlo") {  // a column per statistic, not per sample
        static const QStringList cols = {"n", "mean", "σ", "min", "max", "yield"};
        loading_ = true;
        outputs_->setColumnCount(5 + (int)cols.size());
        for (int c = 0; c < cols.size(); ++c) outputs_->setHorizontalHeaderItem(5 + c, new QTableWidgetItem("MC " + cols[c]));
        int done = 0;
        for (const auto& p : points) done += p.toObject()["status"] != "pending" && p.toObject()["status"] != "running";
        for (int row = 0; row < outputs_->rowCount(); ++row) {
            const QJsonObject st = shownStats_[text(outputs_, row, 1)].toObject();
            for (int c = 0; c < cols.size(); ++c) {
                auto* item = new QTableWidgetItem;
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                if (!st.isEmpty()) {
                    const char* key[] = {"n", "mean", "sd", "min", "max", "yield"};
                    if (st.contains(key[c])) item->setText(c == 0 ? QString::number(st["n"].toInt()) : c == 5 ? QString::number(st["yield"].toDouble() * 100, 'g', 4) + "%"
                                                                                                    : QString::fromStdString(sim::si(st[key[c]].toDouble())));
                }
                outputs_->setItem(row, 5 + c, item);
            }
        }
        outputs_->resizeColumnsToContents();
        loading_ = false;
        if (running()) status_->setText(QString("Running %1 (Monte Carlo): %2 of %3 samples").arg(running_).arg(done).arg(points.size()));
        return;
    }
    loading_ = true;
    outputs_->setColumnCount(5 + (int)points.size());
    int done = 0;
    for (int p = 0; p < points.size(); ++p) {
        const QJsonObject pt = points[p].toObject();
        QStringList vars;
        const QJsonObject v = pt["variables"].toObject();
        for (auto it = v.begin(); it != v.end(); ++it) vars << it.key() + "=" + it.value().toString();
        outputs_->setHorizontalHeaderItem(5 + p, new QTableWidgetItem(pt["test"].toString() + " · " + pt["corner"].toString() +
                                                                       (vars.isEmpty() ? "" : "\n" + vars.join(" "))));
        done += pt["status"] != "pending" && pt["status"] != "running";
        const QJsonObject outs = pt["outputs"].toObject();
        for (int row = 0; row < outputs_->rowCount(); ++row) {
            auto* item = new QTableWidgetItem;
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);  // results are read-only
            const QString out = text(outputs_, row, 1);
            if (text(outputs_, row, 0) != pt["test"].toString() || out.isEmpty()) {
            } else if (pt["status"] != "done") {
                item->setText(pt["status"].toString());
                item->setToolTip(pt["error"].toString());
            } else if (const QJsonObject o = outs[out].toObject(); o.contains("error")) {
                item->setText("error");
                item->setToolTip(o["error"].toString());
                item->setForeground(Qt::red);
            } else {
                item->setText(shown(o["value"]));
                if (o.contains("pass")) item->setForeground(o["pass"].toBool() ? QColor(0, 150, 0) : Qt::red);
            }
            outputs_->setItem(row, 5 + p, item);
        }
    }
    outputs_->resizeColumnsToContents();
    loading_ = false;
    if (running()) status_->setText(QString("Running %1: %2 of %3 points").arg(running_).arg(done).arg(points.size()));
}

}  // namespace app
