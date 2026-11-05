#include "app/test_stage.h"

#include "app/icons.h"
#include "app/theme.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace app {

// ---- DigitalWaves

DigitalWaves::DigitalWaves(QWidget* parent) : QWidget(parent) {
    setMouseTracking(false);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumHeight(120);
    setAccessibleName("Digital waveforms");
    connect(static_cast<ThemeNotifier*>(Theme::notifier()), &ThemeNotifier::changed, this, [this] { update(); });
}

void DigitalWaves::load(const flow::Vcd& vcd) {
    vcd_ = vcd;
    rows_.clear();
    fit();
}

bool DigitalWaves::show(const QString& signal) {
    for (const auto& r : rows_)
        if (r.name == signal) return true;
    for (const auto& s : vcd_.vars) {
        if (QString::fromStdString(s.name) != signal) continue;
        static const std::vector<flow::VcdChange> none;
        const auto it = vcd_.changes.find(s.code);
        rows_.push_back({signal, s.width, it == vcd_.changes.end() ? &none : &it->second});
        update();
        return true;
    }
    return false;
}

void DigitalWaves::hide(const QString& signal) {
    rows_.erase(std::remove_if(rows_.begin(), rows_.end(), [&](const Row& r) { return r.name == signal; }), rows_.end());
    update();
}

void DigitalWaves::clear() {
    rows_.clear();
    update();
}

QStringList DigitalWaves::shown() const {
    QStringList out;
    for (const auto& r : rows_) out << r.name;
    return out;
}

void DigitalWaves::fit() {
    from_ = 0;
    to_ = std::max<int64_t>(vcd_.endTime, 1);
    update();
}

void DigitalWaves::setCursorTime(int64_t t) {
    cursor_ = std::clamp<int64_t>(t, 0, std::max<int64_t>(vcd_.endTime, 1));
    update();
    emit cursorMoved(cursor_);
}

QString DigitalWaves::valueAtCursor(const QString& signal) const {
    for (const auto& r : rows_)
        if (r.name == signal) return QString::fromStdString(flow::toHex(flow::valueAt(*r.changes, cursor_ < 0 ? to_ : cursor_)));
    return {};
}

double DigitalWaves::xOf(int64_t t) const {
    const double w = std::max(1, width() - nameWidth_);
    return nameWidth_ + (double)(t - from_) * w / std::max<double>(1.0, (double)(to_ - from_));
}

int64_t DigitalWaves::timeOf(double x) const {
    const double w = std::max(1, width() - nameWidth_);
    return from_ + (int64_t)std::llround((x - nameWidth_) * (double)(to_ - from_) / w);
}

void DigitalWaves::resizeEvent(QResizeEvent*) { update(); }

void DigitalWaves::wheelEvent(QWheelEvent* e) {
    const double f = std::pow(1.0015, -e->angleDelta().y());
    const double span = std::clamp<double>((double)(to_ - from_) * f, 2.0, (double)std::max<int64_t>(vcd_.endTime, 2));
    const double at = std::max<double>(0, (double)timeOf(e->position().x()));
    const double share = std::clamp((e->position().x() - nameWidth_) / std::max(1, width() - nameWidth_), 0.0, 1.0);
    from_ = std::max<int64_t>(0, (int64_t)(at - span * share));
    to_ = from_ + (int64_t)span;
    if (to_ > vcd_.endTime && vcd_.endTime > 0) {
        to_ = vcd_.endTime;
        from_ = std::max<int64_t>(0, to_ - (int64_t)span);
    }
    update();
    e->accept();
}

void DigitalWaves::mousePressEvent(QMouseEvent* e) {
    if (e->position().x() >= nameWidth_) setCursorTime(timeOf(e->position().x()));
}

void DigitalWaves::mouseMoveEvent(QMouseEvent* e) {
    if ((e->buttons() & Qt::LeftButton) && e->position().x() >= nameWidth_) setCursorTime(timeOf(e->position().x()));
}

void DigitalWaves::paintEvent(QPaintEvent*) {
    const Theme& t = Theme::current();
    QPainter p(this);
    p.fillRect(rect(), t.canvas);
    const int rowHeight = 26, top = 22;
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPixelSize(11);
    p.setFont(mono);

    // The time axis: round steps in the dump's units
    const double unitNs = vcd_.secondsPerUnit * 1e9;
    const double spanNs = (double)(to_ - from_) * unitNs;
    double step = std::pow(10.0, std::floor(std::log10(std::max(spanNs, 1e-6)))) ;
    if (spanNs / step < 4) step /= 2;
    if (spanNs / step > 9) step *= 2;
    p.setPen(t.line);
    for (double n = std::ceil((double)from_ * unitNs / step) * step; n <= (double)to_ * unitNs; n += step) {
        const double x = xOf((int64_t)(n / unitNs));
        p.setPen(t.line);
        p.drawLine(QPointF(x, top), QPointF(x, height()));
        p.setPen(t.muted);
        p.drawText(QPointF(x + 3, 14), QString::number(n, 'g', 6) + " ns");
    }

    for (size_t k = 0; k < rows_.size(); ++k) {
        const Row& row = rows_[k];
        const double y0 = top + (double)k * rowHeight, hi = y0 + 5, lo = y0 + rowHeight - 6, mid = (hi + lo) / 2;
        p.setPen(t.ink);
        const QString leaf = row.name.section('.', -2);
        p.drawText(QRectF(8, y0, nameWidth_ - 14, rowHeight), Qt::AlignVCenter | Qt::AlignLeft,
                   p.fontMetrics().elidedText(leaf + (row.width > 1 ? QString("[%1]").arg(row.width) : QString()), Qt::ElideLeft, nameWidth_ - 14));
        const auto& changes = *row.changes;
        p.setClipRect(QRect(nameWidth_, top, width() - nameWidth_, height() - top));
        auto it = std::upper_bound(changes.begin(), changes.end(), from_, [](int64_t a, const flow::VcdChange& c) { return a < c.time; });
        std::string value = it == changes.begin() ? std::string("x") : std::prev(it)->value;
        int64_t at = from_;
        const auto segment = [&](int64_t a, int64_t b, const std::string& v) {
            const double xa = xOf(a), xb = xOf(b);
            const bool unknown = v.find('x') != std::string::npos, floating = v.find('z') != std::string::npos;
            if (row.width == 1) {
                p.setPen(QPen(unknown ? t.fail : floating ? t.warn : t.pass, 1.3));
                const double y = unknown || floating ? mid : v == "1" ? hi : lo;
                p.drawLine(QPointF(xa, y), QPointF(xb, y));
            } else {
                p.setPen(QPen(unknown ? t.fail : t.via, 1));
                const double slant = std::min(3.0, (xb - xa) / 2);
                const QPolygonF shape({{xa, mid}, {xa + slant, hi}, {xb - slant, hi}, {xb, mid}, {xb - slant, lo}, {xa + slant, lo}, {xa, mid}});
                p.drawPolyline(shape);
                if (xb - xa > 24) {
                    p.setPen(t.ink);
                    const QRectF box(std::max<double>(xa, nameWidth_) + 4, hi, std::min<double>(xb, width()) - std::max<double>(xa, nameWidth_) - 8, lo - hi);
                    p.drawText(box, Qt::AlignVCenter | Qt::AlignLeft, p.fontMetrics().elidedText(QString::fromStdString(flow::toHex(v)), Qt::ElideRight, (int)box.width()));
                }
            }
        };
        // ponytail: a dense signal draws every change in view, so a dump far past a pixel per change is slow to pan; merge
        // changes within a pixel for a very long run
        double lastX = -1e9;
        for (; it != changes.end() && it->time <= to_; ++it) {
            if (row.width == 1 && xOf(it->time) - lastX < 0.5) {
                value = it->value;
                at = it->time;
                continue;
            }
            segment(at, it->time, value);
            if (row.width == 1) {
                const double x = xOf(it->time);
                p.setPen(QPen(t.pass, 1));
                p.drawLine(QPointF(x, hi), QPointF(x, lo));
                lastX = x;
            }
            value = it->value;
            at = it->time;
        }
        segment(at, to_, value);
        p.setClipping(false);
    }
    if (cursor_ >= from_ && cursor_ <= to_) {
        p.setPen(QPen(t.warn, 1));
        p.drawLine(QPointF(xOf(cursor_), 0), QPointF(xOf(cursor_), height()));
    }
    p.setPen(t.line);
    p.drawLine(nameWidth_, 0, nameWidth_, height());
}

// ---- TestRunView

TestRunView::TestRunView(Session& session, const QString& run, QWidget* parent) : QMainWindow(parent, Qt::Window), session_(session), run_(run) {
    setWindowTitle("Test: " + run);
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
    verdict_->setWordWrap(true);
    verdict_->setAccessibleName("Verdict");
    cursorText_ = new QLabel(central);
    top->addWidget(title_);
    top->addSpacing(14);
    top->addWidget(verdict_, 1);
    top->addWidget(cursorText_);
    layout->addLayout(top);

    auto* split = new QSplitter(Qt::Vertical, central);
    auto* upper = new QSplitter(Qt::Horizontal, split);
    auto* left = new QWidget(upper);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);
    filter_ = new QLineEdit(left);
    filter_->setPlaceholderText("Filter signals");
    filter_->setClearButtonEnabled(true);
    signals_ = new QTreeWidget(left);
    signals_->setHeaderHidden(true);
    signals_->setRootIsDecorated(false);
    signals_->setAccessibleName("Recorded signals");
    leftLayout->addWidget(filter_);
    leftLayout->addWidget(signals_, 1);
    waves_ = new DigitalWaves(upper);
    upper->setStretchFactor(0, 1);
    upper->setStretchFactor(1, 4);
    log_ = new QPlainTextEdit(split);
    log_->setReadOnly(true);
    log_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    log_->setAccessibleName("Simulation log");
    split->setStretchFactor(0, 4);
    split->setStretchFactor(1, 1);
    layout->addWidget(split, 1);
    setCentralWidget(central);

    connect(filter_, &QLineEdit::textChanged, this, [this](const QString& text) {
        for (int k = 0; k < signals_->topLevelItemCount(); ++k) {
            auto* i = signals_->topLevelItem(k);
            i->setHidden(!text.isEmpty() && !i->text(0).contains(text, Qt::CaseInsensitive));
        }
    });
    connect(signals_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* i) {
        if (i->checkState(0) == Qt::Checked) waves_->show(i->text(0));
        else waves_->hide(i->text(0));
    });
    connect(waves_, &DigitalWaves::cursorMoved, this, [this](qint64 time) {
        cursorText_->setText(QString::number((double)time * 1e9 * waves_->property("unit").toDouble(), 'g', 8) + " ns");
    });
    poll_ = new QTimer(this);
    poll_->setInterval(1000);
    connect(poll_, &QTimer::timeout, this, [this] { refresh(); });
    connect(static_cast<ThemeNotifier*>(Theme::notifier()), &ThemeNotifier::changed, this, [this] { refresh(); });
    refresh();
}

QString TestRunView::verdict() const { return verdict_->text(); }
QString TestRunView::logText() const { return log_->toPlainText(); }

void TestRunView::refresh() {
    const cmd::Result r = session_.call("test_status", {{"run", run_}});
    if (!r.ok) {
        poll_->stop();
        verdict_->setText(r.error);
        return;
    }
    const QJsonObject st = r.value.toObject();
    const Theme& t = Theme::current();
    const QString state = st["state"].toString(), level = st["level"].toString();
    QString what = run_ + (st["kind"] == "firmware" ? ", firmware" : "");
    what += level == "gate" ? " on the routed netlist" : " on the RTL";
    if (st.contains("flow_run")) what += " of " + st["flow_run"].toString();
    title_->setText(what);
    if (state == "running") {
        verdict_->setText("Running");
        verdict_->setStyleSheet(QString("color: %1;").arg(t.via.name()));
        if (!poll_->isActive()) poll_->start();
    } else {
        poll_->stop();
        const bool passed = st["passed"].toBool();
        QString text = passed ? "Passed" : state == "done" ? "Failed: " + st["error"].toString() : "Run " + state;
        if (st.contains("uart")) text += "   UART: " + st["uart"].toString().trimmed() + "   " + QString::number(st["cycles"].toDouble()) + " cycles";
        verdict_->setText(text);
        verdict_->setStyleSheet(QString("color: %1;").arg((passed ? t.pass : t.fail).name()));
    }
    const QString tail = st["log_tail"].toString();
    if (log_->toPlainText() != tail) {
        log_->setPlainText(tail);
        log_->moveCursor(QTextCursor::End);
    }
    if (st.contains("vcd") && loaded_ != st["vcd"].toString()) loadWaves(st["vcd"].toString());
}

void TestRunView::loadWaves(const QString& vcd) {
    try {
        const flow::Vcd data = flow::readVcd(vcd.toStdString());
        loaded_ = vcd;
        waves_->load(data);
        waves_->setProperty("unit", data.secondsPerUnit);
        signals_->blockSignals(true);
        signals_->clear();
        // The testbench's own signals first: what a person looks at
        std::map<int, std::vector<const flow::VcdSignal*>> byDepth;
        for (const auto& s : data.vars) byDepth[(int)std::count(s.name.begin(), s.name.end(), '.')].push_back(&s);
        int added = 0;
        for (const auto& [depth, list] : byDepth)
            for (const flow::VcdSignal* s : list) {
                auto* item = new QTreeWidgetItem(signals_, {QString::fromStdString(s->name)});
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                const bool show = depth <= 1 && added < 16;
                item->setCheckState(0, show ? Qt::Checked : Qt::Unchecked);
                if (show) {
                    waves_->show(item->text(0));
                    ++added;
                }
            }
        signals_->blockSignals(false);
    } catch (const std::exception& e) {
        verdict_->setText(QString("Its dump cannot be read: ") + e.what());
    }
}

// ---- A new test

namespace {

class NewTestDialog : public QDialog {
public:
    NewTestDialog(Session& session, QWidget* parent) : QDialog(parent), session_(session) {
        setWindowTitle("New test");
        auto* form = new QFormLayout(this);
        name_ = new QLineEdit(this);
        name_->setPlaceholderText("letters, digits and _");
        files_ = new QListWidget(this);
        files_->setMinimumHeight(60);
        auto* add = new QPushButton("Add testbench…", this);
        connect(add, &QPushButton::clicked, this, [this] {
            for (const QString& f : QFileDialog::getOpenFileNames(this, "Testbench", {}, "Verilog (*.v *.sv);;All files (*)")) files_->addItem(f);
            if (top_->text().isEmpty() && files_->count()) top_->setText(QFileInfo(files_->item(0)->text()).baseName());
            if (name_->text().isEmpty() && files_->count()) name_->setText(QFileInfo(files_->item(0)->text()).baseName());
        });
        top_ = new QLineEdit(this);
        top_->setPlaceholderText("the testbench's module");
        level_ = new QComboBox(this);
        level_->addItem("The routed netlist, with its layout's delays", "gate");
        level_->addItem("The RTL", "rtl");
        run_ = new QComboBox(this);
        for (const auto& v : session_.call("flow_list").value.toArray())
            if (v.toObject()["state"] == "done") run_->addItem(v.toObject()["run"].toString() + "  " + v.toObject()["platform"].toString(), v.toObject()["run"].toString());
        dut_ = new QLineEdit("tb.dut", this);
        pattern_ = new QLineEdit("PASS", this);
        limit_ = new QDoubleSpinBox(this);
        limit_->setRange(1, 1e12);
        limit_->setDecimals(0);
        limit_->setValue(1e6);
        limit_->setSuffix(" ns");
        form->addRow("Name", name_);
        form->addRow("Testbench", files_);
        form->addRow("", add);
        form->addRow("Its top module", top_);
        form->addRow("What runs", level_);
        form->addRow("Implementation run", run_);
        form->addRow("Design's instance", dut_);
        form->addRow("Passes when it prints", pattern_);
        form->addRow("Time limit", limit_);
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
        QJsonArray bench;
        for (int k = 0; k < files_->count(); ++k) bench.append(files_->item(k)->text());
        QJsonObject args{{"name", name_->text()}, {"testbench", bench}, {"top", top_->text()}, {"level", level_->currentData().toString()},
                         {"pass_pattern", pattern_->text()}, {"time_limit_ns", limit_->value()}, {"wait", false}};
        if (run_->count()) args["flow_run"] = run_->currentData().toString();
        if (level_->currentData() == "gate") args["dut"] = dut_->text();
        const cmd::Result r = session_.call("test_run", args);
        if (!r.ok) {
            error_->setText(r.error);
            return;
        }
        started = r.value.toObject()["run"].toString();
        accept();
    }
    Session& session_;
    QLineEdit* name_;
    QLineEdit* top_;
    QLineEdit* dut_;
    QLineEdit* pattern_;
    QListWidget* files_;
    QComboBox* level_;
    QComboBox* run_;
    QDoubleSpinBox* limit_;
    QLabel* error_;
};

}  // namespace

// ---- TestPanel

TestPanel::TestPanel(Session& session, Actions actions, QWidget* parent) : QWidget(parent), session_(session), actions_(std::move(actions)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* start = new QPushButton("New test…", this);
    start->setIcon(icons::get("add"));
    connect(start, &QPushButton::clicked, this, [this] { newTest(); });
    layout->addWidget(start);
    tree_ = new QTreeWidget(this);
    tree_->setHeaderHidden(true);
    tree_->setAccessibleName("Tests");
    layout->addWidget(tree_, 1);
    const auto open = [this](QTreeWidgetItem* i) {
        const QString run = i->data(0, Qt::UserRole).toString();
        if (!run.isEmpty() && actions_.open) actions_.open(run);
    };
    connect(tree_, &QTreeWidget::itemActivated, this, open);
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, open);
    poll_ = new QTimer(this);
    poll_->setInterval(3000);
    connect(poll_, &QTimer::timeout, this, [this] { if (isVisible()) refresh(); });
    poll_->start();
    refresh();
}

void TestPanel::refresh() {
    const cmd::Result r = session_.call("test_list");
    if (!r.ok) return;
    const QString keep = tree_->currentItem() ? tree_->currentItem()->data(0, Qt::UserRole).toString() : QString();
    const Theme& t = Theme::current();
    std::map<QString, QTreeWidgetItem*> tests;
    tree_->clear();
    for (const auto& v : r.value.toArray()) {
        const QJsonObject o = v.toObject();
        const QString run = o["run"].toString(), name = run.section('/', 0, 0);
        auto [it, fresh] = tests.try_emplace(name, nullptr);
        if (fresh) {
            it->second = new QTreeWidgetItem(tree_, {name});
            it->second->setExpanded(true);
        }
        const QString state = o["state"].toString();
        const QString result = state == "running" ? "running" : o["passed"].toBool() ? "passed" : state == "done" ? "failed" : state;
        auto* item = new QTreeWidgetItem(it->second, {run.section('/', 1) + "  " + o["level"].toString() + "  " + result});
        item->setData(0, Qt::UserRole, run);
        item->setForeground(0, result == "passed" ? t.pass : result == "running" ? t.via : t.fail);
        if (run == keep) tree_->setCurrentItem(item);
    }
}

void TestPanel::newTest() {
    NewTestDialog dialog(session_, this);
    if (dialog.exec() != QDialog::Accepted) return;
    refresh();
    if (actions_.open) actions_.open(dialog.started);
}

}  // namespace app
