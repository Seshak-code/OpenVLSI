#include "app/implement.h"

#include "app/icons.h"
#include "app/theme.h"
#include "app/ui_commands.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cmath>
#include <map>
#include <set>

namespace app {

namespace {

QString stageTitle(const QString& stage) {
    static const std::map<QString, QString> names = {{"synth", "Synthesis"}, {"floorplan", "Floorplan"}, {"place", "Place"}, {"cts", "Clock tree"},
                                                     {"route", "Route"}, {"finish", "Finish"}, {"drc", "DRC"}, {"lvs", "LVS"}};
    const auto it = names.find(stage);
    return it == names.end() ? stage : it->second;
}

QColor stateColour(const QString& state) {
    const Theme& t = Theme::current();
    if (state == "done") return t.pass;
    if (state == "running") return t.via;
    if (state == "failed" || state == "interrupted") return t.fail;
    if (state == "stopped" || state == "unavailable") return t.warn;
    return t.muted;
}

QString duration(double seconds) {
    if (seconds < 0) return {};
    if (seconds < 60) return QString::number(seconds, 'f', seconds < 10 ? 1 : 0) + " s";
    return QString("%1 m %2 s").arg((int)(seconds / 60)).arg((int)seconds % 60);
}

// The rows of the metrics table, in reading order
struct Row { const char* key; const char* title; const char* unit; double scale; };
const Row kRows[] = {
    {"instances", "Cells", "", 1},
    {"area_um2", "Cell area", "µm²", 1},
    {"die_area_um2", "Die area", "µm²", 1},
    {"utilization", "Utilization", "%", 100},
    {"setup_slack_ns", "Setup slack", "ns", 1},
    {"wns_ns", "Worst negative slack", "ns", 1},
    {"tns_ns", "Total negative slack", "ns", 1},
    {"hold_slack_ns", "Hold slack", "ns", 1},
    {"power_w", "Power", "mW", 1e3},
    {"wirelength_um", "Wirelength", "µm", 1},
    {"vias", "Vias", "", 1},
    {"drc_errors", "Routing DRC errors", "", 1},
    {"antenna_nets", "Antenna violations", "", 1},
    {"violations", "DRC violations", "", 1},
    {"devices_layout", "Layout devices", "", 1},
    {"mismatches", "LVS mismatches", "", 1},
};

// A value's verdict where one follows from it alone: slack below zero, any violation
bool isBad(const QString& key, double value) {
    if (key == "setup_slack_ns" || key == "hold_slack_ns" || key == "wns_ns" || key == "tns_ns") return value < 0;
    if (key == "drc_errors" || key == "antenna_nets" || key == "violations" || key == "mismatches") return value > 0;
    return false;
}

}  // namespace

// ---- StagePipeline

StagePipeline::StagePipeline(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumHeight(66);
    setAccessibleName("Flow stages");
    connect(static_cast<ThemeNotifier*>(Theme::notifier()), &ThemeNotifier::changed, this, [this] { update(); });
}

QSize StagePipeline::sizeHint() const { return {720, 70}; }

void StagePipeline::setStages(const QJsonArray& stages) {
    nodes_.clear();
    QStringList spoken;
    for (const auto& v : stages) {
        const QJsonObject o = v.toObject();
        nodes_.push_back({o["name"].toString(), o["state"].toString(), o.contains("elapsed_s") ? o["elapsed_s"].toDouble() : -1});
        spoken << stageTitle(o["name"].toString()) + " " + o["state"].toString();
    }
    setAccessibleDescription(spoken.join(", "));
    update();
}

QString StagePipeline::stateOf(const QString& stage) const {
    for (const auto& n : nodes_)
        if (n.name == stage) return n.state;
    return {};
}

void StagePipeline::paintEvent(QPaintEvent*) {
    if (nodes_.empty()) return;
    const Theme& t = Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int n = (int)nodes_.size();
    const double margin = 46, step = n > 1 ? (width() - 2 * margin) / (n - 1) : 0, y = 24, r = 7;
    QFont small = font();
    small.setPixelSize(11);
    QFont normal = font();
    normal.setPixelSize(12);
    for (int k = 0; k + 1 < n; ++k) {
        const bool passed = nodes_[k].state == "done";
        p.setPen(QPen(passed ? t.pass : t.line, 1.5));
        p.drawLine(QPointF(margin + k * step + r, y), QPointF(margin + (k + 1) * step - r, y));
    }
    for (int k = 0; k < n; ++k) {
        const Node& node = nodes_[k];
        const QPointF at(margin + k * step, y);
        const QColor c = stateColour(node.state);
        if (node.state == "pending" || node.state == "skipped" || node.state == "unavailable") {
            QPen pen(c, 1.5);
            if (node.state != "pending") pen.setStyle(Qt::DotLine);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(at, r, r);
        } else {
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawEllipse(at, r, r);
            // The state is its shape too, not only its colour: a tick, a cross, a bar
            p.setPen(QPen(t.dark ? t.window : QColor(Qt::white), 1.6, Qt::SolidLine, Qt::RoundCap));
            if (node.state == "done") {
                p.drawLine(at + QPointF(-3.2, 0.2), at + QPointF(-0.9, 2.6));
                p.drawLine(at + QPointF(-0.9, 2.6), at + QPointF(3.4, -2.8));
            } else if (node.state == "failed" || node.state == "interrupted") {
                p.drawLine(at + QPointF(-2.8, -2.8), at + QPointF(2.8, 2.8));
                p.drawLine(at + QPointF(-2.8, 2.8), at + QPointF(2.8, -2.8));
            } else if (node.state == "stopped") {
                p.drawLine(at + QPointF(-3, 0), at + QPointF(3, 0));
            }
        }
        p.setBrush(Qt::NoBrush);
        p.setFont(normal);
        p.setPen(node.state == "pending" ? t.muted : t.ink);
        const QRectF label(at.x() - step / 2 - 20, y + 10, step + 40, 16);
        p.drawText(label, Qt::AlignHCenter | Qt::AlignVCenter, stageTitle(node.name));
        p.setFont(small);
        p.setPen(t.muted);
        const QString under = node.state == "unavailable" ? QString("no open deck") : node.state == "skipped" ? QString("skipped") : duration(node.elapsed);
        p.drawText(QRectF(at.x() - step / 2 - 20, y + 26, step + 40, 14), Qt::AlignHCenter | Qt::AlignVCenter, under);
    }
}

// ---- FlowRunView

FlowRunView::FlowRunView(Session& session, const QString& run, QWidget* parent)
    : QMainWindow(parent, Qt::Window), session_(session), run_(run) {
    setWindowTitle("Implement: " + run);
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(14, 10, 14, 6);
    layout->setSpacing(8);

    auto* top = new QHBoxLayout();
    title_ = new QLabel(central);
    QFont big = title_->font();
    big.setPixelSize(15);
    big.setWeight(QFont::DemiBold);
    title_->setFont(big);
    verdict_ = new QLabel(central);
    verdict_->setAccessibleName("Verdict");
    stop_ = new QPushButton("Stop", central);
    layout_ = new QPushButton("Open layout", central);
    markers_ = new QPushButton("Show DRC markers", central);
    timing_ = new QPushButton("Timing", central);
    power_ = new QPushButton("IR drop", central);
    top->addWidget(title_);
    top->addSpacing(14);
    top->addWidget(verdict_);
    top->addStretch(1);
    top->addWidget(stop_);
    top->addWidget(markers_);
    top->addWidget(timing_);
    top->addWidget(power_);
    top->addWidget(layout_);
    layout->addLayout(top);

    pipeline_ = new StagePipeline(central);
    layout->addWidget(pipeline_);

    reasons_ = new QLabel(central);
    reasons_->setWordWrap(true);
    reasons_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(reasons_);

    auto* split = new QSplitter(Qt::Vertical, central);
    metrics_ = new QTableWidget(split);
    metrics_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    metrics_->setSelectionMode(QAbstractItemView::SingleSelection);
    metrics_->verticalHeader()->setDefaultSectionSize(24);
    metrics_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    log_ = new QPlainTextEdit(split);
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(5000);
    log_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    log_->setAccessibleName("Flow log");
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    layout->addWidget(split, 1);
    setCentralWidget(central);

    connect(stop_, &QPushButton::clicked, this, [this] {
        const cmd::Result r = session_.call("flow_stop", {{"run", run_}});
        if (!r.ok) reasons_->setText(r.error);
        refresh();
    });
    connect(layout_, &QPushButton::clicked, this, [this] { emit openLayoutRequested(run_); });
    connect(markers_, &QPushButton::clicked, this, [this] { emit showMarkersRequested(run_); });
    connect(timing_, &QPushButton::clicked, this, [this] { emit signoffRequested(run_, true); });
    connect(power_, &QPushButton::clicked, this, [this] { emit signoffRequested(run_, false); });
    bindActions(this, {{"Stop", "flow_stop"}, {"Open layout", "flow_open_layout"}, {"Show DRC markers", "layout_view_markers"}, {"Timing", "timing_view"}, {"IR drop", "power_view"}});

    poll_ = new QTimer(this);
    poll_->setInterval(1000);
    connect(poll_, &QTimer::timeout, this, [this] { refresh(); });
    connect(static_cast<ThemeNotifier*>(Theme::notifier()), &ThemeNotifier::changed, this, [this] {
        lastFingerprint_.clear();
        refresh();
    });
    refresh();
}

QString FlowRunView::verdict() const { return verdict_->text(); }
QString FlowRunView::logText() const { return log_->toPlainText(); }
QString FlowRunView::reasons() const { return reasons_->text(); }

void FlowRunView::refresh() {
    const cmd::Result r = session_.call("flow_status", {{"run", run_}, {"tail", 400}});
    if (!r.ok) {
        poll_->stop();
        verdict_->setText("Unreadable");
        reasons_->setText(r.error);
        return;
    }
    const QJsonObject st = r.value.toObject();
    const QString state = st["state"].toString();
    const bool running = state == "running";
    title_->setText(QString("%1 on %2, %3 ns clock").arg(run_, st["platform"].toString()).arg(st["clock_period_ns"].toDouble()));
    pipeline_->setStages(st["stages"].toArray());
    stop_->setVisible(running);

    QStringList tailLines;
    for (const auto& line : st["log_tail"].toArray()) tailLines << line.toString();
    const QString tail = tailLines.join('\n');
    QScrollBar* bar = log_->verticalScrollBar();
    const bool follow = bar->value() >= bar->maximum() - 2;
    if (log_->toPlainText() != tail) {
        const int at = bar->value();
        log_->setPlainText(tail);
        bar->setValue(follow ? bar->maximum() : at);
    }

    // A report only when something finished: reading it is several files per stage
    QString fingerprint = state;
    for (const auto& v : st["stages"].toArray()) fingerprint += v.toObject()["state"].toString();
    if (fingerprint != lastFingerprint_) {
        lastFingerprint_ = fingerprint;
        const cmd::Result rep = session_.call("flow_report", {{"run", run_}});
        if (rep.ok) fillReport(rep.value.toObject());
        else reasons_->setText(rep.error);
    }
    if (running) {
        verdict_->setText("Running" + (st["stage"].isString() ? ": " + stageTitle(st["stage"].toString()) : QString()));
        verdict_->setStyleSheet(QString("color: %1;").arg(Theme::current().via.name()));
        if (!poll_->isActive()) poll_->start();
        reasons_->clear();
    } else {
        poll_->stop();
    }
}

void FlowRunView::fillReport(const QJsonObject& report) {
    const QJsonArray stages = report["stages"].toArray();
    const Theme& t = Theme::current();
    std::vector<const Row*> rows;
    for (const Row& row : kRows)
        for (const auto& s : stages)
            if (s.toObject()["metrics"].toObject().contains(row.key)) {
                rows.push_back(&row);
                break;
            }
    metrics_->clear();
    metrics_->setColumnCount((int)stages.size());
    metrics_->setRowCount((int)rows.size());
    QStringList headers;
    for (const auto& s : stages) headers << stageTitle(s.toObject()["stage"].toString());
    metrics_->setHorizontalHeaderLabels(headers);
    QStringList titles;
    for (const Row* row : rows) titles << (QString(row->title) + (*row->unit ? QString(" (%1)").arg(row->unit) : QString()));
    metrics_->setVerticalHeaderLabels(titles);
    for (int c = 0; c < (int)stages.size(); ++c) {
        const QJsonObject m = stages[c].toObject()["metrics"].toObject();
        for (int k = 0; k < (int)rows.size(); ++k) {
            if (!m.contains(rows[k]->key)) continue;
            const double value = m[rows[k]->key].toDouble() * rows[k]->scale;
            const bool whole = value == std::floor(value) && std::abs(value) < 1e9;
            auto* item = new QTableWidgetItem(whole ? QLocale().toString((qlonglong)value) : QString::number(value, 'f', std::abs(value) < 10 ? 3 : 1));
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            if (isBad(rows[k]->key, value)) item->setForeground(t.fail);
            metrics_->setItem(k, c, item);
        }
    }
    const QString state = report["state"].toString();
    const QJsonArray why = report["not_clean"].toArray();
    bool hasGds = false;
    for (const auto& s : stages) hasGds = hasGds || s.toObject()["metrics"].toObject().contains("gds");
    layout_->setEnabled(hasGds);
    int violations = 0;
    for (const auto& s : stages)
        if (s.toObject()["stage"] == "drc") violations = s.toObject()["metrics"].toObject()["violations"].toInt();
    markers_->setVisible(hasGds && violations > 0);
    timing_->setEnabled(hasGds);
    power_->setEnabled(hasGds);
    if (state == "running") return;
    const bool clean = report["clean"].toBool();
    verdict_->setText(clean ? "Clean: timing met, DRC clean, LVS matched" : state == "done" ? "Finished, not clean" : "Run " + state);
    verdict_->setStyleSheet(QString("color: %1;").arg((clean ? t.pass : t.fail).name()));
    QStringList lines;
    for (const auto& w : why) lines << w.toString();
    reasons_->setText(lines.join("\n"));
    reasons_->setVisible(!lines.isEmpty());
}

// ---- A new run

namespace {

class NewRunDialog : public QDialog {
public:
    NewRunDialog(Session& session, QWidget* parent) : QDialog(parent), session_(session) {
        setWindowTitle("New implementation run");
        auto* form = new QFormLayout(this);
        design_ = new QLineEdit(this);
        design_->setPlaceholderText("letters, digits and _");
        top_ = new QLineEdit(this);
        top_->setPlaceholderText("the top module");
        platform_ = new QComboBox(this);
        for (const auto& p : session_.call("flow_platforms").value.toArray()) {
            const QJsonObject o = p.toObject();
            platform_->addItem(o["name"].toString() + (o["usable"].toBool() ? "" : " (not installed)"), o["name"].toString());
        }
        clock_ = new QDoubleSpinBox(this);
        clock_->setRange(0.001, 100000);
        clock_->setDecimals(3);
        clock_->setValue(10);
        clock_->setSuffix(" ns");
        port_ = new QLineEdit("clk", this);
        util_ = new QDoubleSpinBox(this);
        util_->setRange(1, 95);
        util_->setValue(40);
        util_->setSuffix(" %");
        files_ = new QListWidget(this);
        files_->setMinimumHeight(70);
        auto* add = new QPushButton("Add Verilog…", this);
        connect(add, &QPushButton::clicked, this, [this] {
            for (const QString& f : QFileDialog::getOpenFileNames(this, "Verilog sources", {}, "Verilog (*.v *.sv);;All files (*)")) files_->addItem(f);
            if (top_->text().isEmpty() && files_->count()) top_->setText(QFileInfo(files_->item(0)->text()).baseName());
            if (design_->text().isEmpty() && files_->count()) design_->setText(QFileInfo(files_->item(0)->text()).baseName());
        });
        form->addRow("Design", design_);
        form->addRow("Sources", files_);
        form->addRow("", add);
        form->addRow("Top module", top_);
        form->addRow("Platform", platform_);
        form->addRow("Clock period", clock_);
        form->addRow("Clock port", port_);
        form->addRow("Core utilization", util_);
        error_ = new QLabel(this);
        error_->setWordWrap(true);
        error_->setStyleSheet(QString("color: %1;").arg(Theme::current().fail.name()));
        form->addRow(error_);
        auto* buttons = new QDialogButtonBox(this);
        buttons->addButton("Run", QDialogButtonBox::AcceptRole);
        buttons->addButton(QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::accepted, this, [this] { start(); });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        form->addRow(buttons);
    }
    QString started;

private:
    void start() {
        QJsonArray verilog;
        for (int k = 0; k < files_->count(); ++k) verilog.append(files_->item(k)->text());
        const cmd::Result r = session_.call("flow_run", {{"design", design_->text()}, {"verilog", verilog}, {"top", top_->text()},
                                                         {"platform", platform_->currentData().toString()}, {"clock_period_ns", clock_->value()},
                                                         {"clock_port", port_->text()}, {"core_utilization", util_->value()}, {"wait", false}});
        if (!r.ok) {
            error_->setText(r.error);  // the form stays open, with what was wrong
            return;
        }
        started = r.value.toObject()["run"].toString();
        accept();
    }
    Session& session_;
    QLineEdit* design_;
    QLineEdit* top_;
    QLineEdit* port_;
    QComboBox* platform_;
    QDoubleSpinBox* clock_;
    QDoubleSpinBox* util_;
    QListWidget* files_;
    QLabel* error_;
};

}  // namespace

// ---- ImplementPanel

ImplementPanel::ImplementPanel(Session& session, Actions actions, QWidget* parent)
    : QWidget(parent), session_(session), actions_(std::move(actions)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* start = new QPushButton("New run…", this);
    start->setIcon(icons::get("add"));
    connect(start, &QPushButton::clicked, this, [this] { newRun(); });
    start->setProperty("api", "flow_run");
    layout->addWidget(start);
    tree_ = new QTreeWidget(this);
    tree_->setHeaderHidden(true);
    tree_->setIconSize({16, 16});
    tree_->setAccessibleName("Implementation runs");
    layout->addWidget(tree_, 1);
    tools_ = new QLabel(this);
    tools_->setWordWrap(true);
    tools_->setContentsMargins(8, 6, 8, 6);
    layout->addWidget(tools_);
    connect(tree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* i) {
        const QString run = i->data(0, Qt::UserRole).toString();
        if (!run.isEmpty() && actions_.open) actions_.open(run);
    });
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* i) {
        const QString run = i->data(0, Qt::UserRole).toString();
        if (!run.isEmpty() && actions_.open) actions_.open(run);
    });
    poll_ = new QTimer(this);
    poll_->setInterval(3000);
    connect(poll_, &QTimer::timeout, this, [this] { if (isVisible()) refresh(); });
    poll_->start();

    QStringList missing;
    const cmd::Result found = session_.call("flow_tools");
    for (const auto& t : found.value.toArray())
        if (!t.toObject()["found"].toBool()) missing << t.toObject()["name"].toString();
    tools_->setText(missing.isEmpty() ? QString() : "Missing: " + missing.join(", ") + ". Run scripts/setup_eda_tools.sh, or set their variables.");
    tools_->setVisible(!missing.isEmpty());
    refresh();
}

QString ImplementPanel::toolsText() const { return tools_->text(); }

void ImplementPanel::refresh() {
    const QString keep = tree_->currentItem() ? tree_->currentItem()->data(0, Qt::UserRole).toString() : QString();
    const cmd::Result r = session_.call("flow_list");
    if (!r.ok) return;
    std::map<QString, QTreeWidgetItem*> designs;
    std::set<QString> open;
    for (int k = 0; k < tree_->topLevelItemCount(); ++k)
        if (!tree_->topLevelItem(k)->isExpanded()) open.insert(tree_->topLevelItem(k)->text(0));
    tree_->clear();
    for (const auto& v : r.value.toArray()) {
        const QJsonObject o = v.toObject();
        const QString run = o["run"].toString(), design = run.section('/', 0, 0);
        auto [it, fresh] = designs.try_emplace(design, nullptr);
        if (fresh) {
            it->second = new QTreeWidgetItem(tree_, {design});
            it->second->setIcon(0, icons::get("implement"));
            it->second->setExpanded(!open.count(design));
        }
        const QString state = o["state"].toString();
        auto* item = new QTreeWidgetItem(it->second, {run.section('/', 1) + "  " + o["platform"].toString() + "  " + state});
        item->setData(0, Qt::UserRole, run);
        item->setForeground(0, stateColour(state));
        if (run == keep) tree_->setCurrentItem(item);
    }
}

void ImplementPanel::newRun() {
    NewRunDialog dialog(session_, this);
    if (dialog.exec() != QDialog::Accepted) return;
    refresh();
    if (actions_.open) actions_.open(dialog.started);
}

}  // namespace app
