#include "app/signoff.h"

#include "app/theme.h"
#include "app/ui_commands.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace app {

namespace {

QTableWidgetItem* cell(const QString& text, bool right = false) {
    auto* item = new QTableWidgetItem(text);
    if (right) item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

QString number(double v, int digits = 3) { return QString::number(v, 'f', digits); }

}  // namespace

SignoffView::SignoffView(Session& session, const QString& run, Kind kind, QWidget* parent)
    : QMainWindow(parent, Qt::Window), session_(session), run_(run), kind_(kind) {
    setWindowTitle((kind == Kind::Timing ? "Timing: " : "Power grid: ") + run);
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(14, 10, 14, 6);
    layout->setSpacing(8);
    auto* top = new QHBoxLayout();
    title_ = new QLabel((kind == Kind::Timing ? "Timing of " : "Power grid of ") + run, central);
    QFont big = title_->font();
    big.setPixelSize(15);
    big.setWeight(QFont::DemiBold);
    title_->setFont(big);
    analyze_ = new QPushButton("Analyze", central);
    show_ = new QPushButton("Show on layout", central);
    top->addWidget(title_);
    top->addStretch(1);
    top->addWidget(show_);
    top->addWidget(analyze_);
    layout->addLayout(top);
    summary_ = new QLabel(central);
    summary_->setWordWrap(true);
    summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    summary_->setAccessibleName("Summary");
    layout->addWidget(summary_);
    auto* split = new QSplitter(Qt::Vertical, central);
    list_ = new QTableWidget(split);
    detail_ = new QTableWidget(split);
    for (QTableWidget* t : {list_, detail_}) {
        t->setEditTriggers(QAbstractItemView::NoEditTriggers);
        t->setSelectionBehavior(QAbstractItemView::SelectRows);
        t->setSelectionMode(QAbstractItemView::SingleSelection);
        t->verticalHeader()->setVisible(false);
        t->verticalHeader()->setDefaultSectionSize(24);
        t->horizontalHeader()->setStretchLastSection(true);
    }
    list_->setAccessibleName(kind == Kind::Timing ? "Worst paths" : "Power nets");
    detail_->setAccessibleName(kind == Kind::Timing ? "Selected path" : "Selected net");
    if (kind == Kind::Power) detail_->hide();
    layout->addWidget(split, 1);
    setCentralWidget(central);

    connect(analyze_, &QPushButton::clicked, this, [this] {
        const cmd::Result r = session_.call(kind_ == Kind::Timing ? "timing_analyze" : "power_analyze", {{"run", run_}, {"wait", false}});
        summary_->setText(r.ok ? "Running" : r.error);
        poll_->start();
    });
    connect(show_, &QPushButton::clicked, this, [this] { showOnLayout(); });
    connect(list_, &QTableWidget::currentCellChanged, this, [this](int row) { selectRow(row); });
    analyze_->setProperty("api", kind == Kind::Timing ? "timing_analyze" : "power_analyze");
    show_->setProperty("api", kind == Kind::Timing ? "layout_view_path" : "layout_view_power");
    poll_ = new QTimer(this);
    poll_->setInterval(1000);
    connect(poll_, &QTimer::timeout, this, [this] { refresh(); });
    connect(static_cast<ThemeNotifier*>(Theme::notifier()), &ThemeNotifier::changed, this, [this] { refresh(); });
    refresh();
}

QString SignoffView::summary() const { return summary_->text(); }

void SignoffView::refresh() {
    const cmd::Result r = session_.call(kind_ == Kind::Timing ? "timing_report" : "power_report", {{"run", run_}});
    const Theme& t = Theme::current();
    if (!r.ok) {
        poll_->stop();
        summary_->setText("Not analyzed yet. Press Analyze.");
        summary_->setStyleSheet({});
        show_->setEnabled(false);
        return;
    }
    report_ = r.value.toObject();
    const QString state = report_["state"].toString();
    if (state == "running") {
        summary_->setText("Running");
        summary_->setStyleSheet(QString("color: %1;").arg(t.via.name()));
        if (!poll_->isActive()) poll_->start();
        return;
    }
    poll_->stop();
    if (state != "done") {
        summary_->setText("Did not finish: " + report_["error"].toString());
        summary_->setStyleSheet(QString("color: %1;").arg(t.fail.name()));
        show_->setEnabled(false);
        return;
    }
    show_->setEnabled(true);
    list_->clearContents();
    if (kind_ == Kind::Timing) {
        const bool met = report_["met"].toBool();
        QStringList clocks;
        for (const auto& c : report_["clocks"].toArray())
            clocks << QString("%1: minimum period %2 ns, %3 MHz").arg(c.toObject()["clock"].toString(), number(c.toObject()["min_period_ns"].toDouble()), number(c.toObject()["fmax_mhz"].toDouble(), 1));
        summary_->setText(QString(met ? "Timing met. " : "Timing not met. ") + QString("WNS %1 ns, TNS %2 ns. ").arg(number(report_["wns_ns"].toDouble()), number(report_["tns_ns"].toDouble())) +
                          clocks.join("; ") + ". Parasitics " + report_["parasitics"].toString() + ".");
        summary_->setStyleSheet(QString("color: %1;").arg((met ? t.pass : t.fail).name()));
        const QJsonArray setup = report_["setup"].toArray(), hold = report_["hold"].toArray();
        list_->setColumnCount(5);
        list_->setHorizontalHeaderLabels({"Check", "Slack (ns)", "Arrival (ns)", "From", "To"});
        list_->setRowCount((int)(setup.size() + hold.size()));
        int row = 0;
        for (const auto& [name, group] : {std::pair{"Setup", setup}, {"Hold", hold}})
            for (const auto& v : group) {
                const QJsonObject p = v.toObject();
                list_->setItem(row, 0, cell(name));
                auto* slack = cell(number(p["slack_ns"].toDouble()), true);
                if (p["slack_ns"].toDouble() < 0) slack->setForeground(t.fail);
                list_->setItem(row, 1, slack);
                list_->setItem(row, 2, cell(number(p["arrival_ns"].toDouble()), true));
                list_->setItem(row, 3, cell(p["startpoint"].toString()));
                list_->setItem(row, 4, cell(p["endpoint"].toString()));
                ++row;
            }
    } else {
        const QJsonArray nets = report_["nets"].toArray();
        double worst = 0;
        for (const auto& n : nets) worst = std::max(worst, n.toObject()["worst_drop_mv"].toDouble());
        const double percent = report_["supply_v"].toDouble() > 0 ? worst / (report_["supply_v"].toDouble() * 1e3) * 100 : 0;
        summary_->setText(QString("Worst drop %1 mV, %2 % of the %3 V supply.").arg(number(worst, 2), number(percent, 2)).arg(report_["supply_v"].toDouble()));
        summary_->setStyleSheet(QString("color: %1;").arg((percent < 5 ? t.pass : t.warn).name()));
        list_->setColumnCount(7);
        list_->setHorizontalHeaderLabels({"Net", "Worst drop (mV)", "Average (mV)", "Worst (%)", "Largest current (mA)", "Nodes", "Worst at"});
        list_->setRowCount((int)nets.size());
        for (int k = 0; k < (int)nets.size(); ++k) {
            const QJsonObject n = nets[k].toObject();
            list_->setItem(k, 0, cell(n["net"].toString()));
            list_->setItem(k, 1, cell(number(n["worst_drop_mv"].toDouble(), 3), true));
            list_->setItem(k, 2, cell(number(n["average_drop_mv"].toDouble(), 3), true));
            list_->setItem(k, 3, cell(number(n["worst_percent"].toDouble(), 3), true));
            list_->setItem(k, 4, cell(number(n["max_current_ma"].toDouble(), 4), true));
            list_->setItem(k, 5, cell(QLocale().toString((qlonglong)n["nodes"].toDouble()), true));
            list_->setItem(k, 6, cell(n["worst_instance"].toString()));
        }
    }
    list_->resizeColumnsToContents();
    if (list_->rowCount()) selectRow(0);
}

void SignoffView::selectRow(int row) {
    if (kind_ != Kind::Timing || row < 0) return;
    if (list_->currentRow() != row) {
        list_->blockSignals(true);
        list_->selectRow(row);
        list_->blockSignals(false);
    }
    const QJsonArray setup = report_["setup"].toArray(), hold = report_["hold"].toArray();
    const QJsonObject path = (row < setup.size() ? setup[row] : hold[row - setup.size()]).toObject();
    const QJsonArray steps = path["steps"].toArray();
    detail_->clear();
    detail_->setColumnCount(6);
    detail_->setHorizontalHeaderLabels({"Pin", "Cell", "Net", "Arrival (ns)", "Slew (ns)", "Where (µm)"});
    detail_->setRowCount((int)steps.size());
    for (int k = 0; k < (int)steps.size(); ++k) {
        const QJsonObject s = steps[k].toObject();
        detail_->setItem(k, 0, cell(s["pin"].toString()));
        detail_->setItem(k, 1, cell(s["cell"].toString()));
        detail_->setItem(k, 2, cell(s["net"].toString()));
        detail_->setItem(k, 3, cell(number(s["arrival_ns"].toDouble()), true));
        detail_->setItem(k, 4, cell(number(s["slew_ns"].toDouble()), true));
        detail_->setItem(k, 5, cell(QString("%1, %2").arg(number(s["x_um"].toDouble(), 2), number(s["y_um"].toDouble(), 2))));
    }
    detail_->resizeColumnsToContents();
}

bool SignoffView::showOnLayout() {
    const cmd::Result opened = session_.call("flow_open_layout", {{"run", run_}});
    if (!opened.ok) {
        summary_->setText(opened.error);
        return false;
    }
    QJsonObject args = opened.value.toObject();
    args["run"] = run_;
    if (kind_ == Kind::Timing) {
        args["index"] = std::max(0, list_->currentRow());
        return session_.call("layout_view_path", args).ok;
    }
    const int row = std::max(0, list_->currentRow());
    args["net"] = list_->item(row, 0) ? list_->item(row, 0)->text() : QString("VDD");
    return session_.call("layout_view_power", args).ok;
}

}  // namespace app
