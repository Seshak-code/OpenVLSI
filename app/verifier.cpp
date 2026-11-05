#include "app/verifier.h"

#include "app/palette.h"
#include "app/theme.h"

#include <QAction>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QTreeWidget>

#include <map>
#include <set>

namespace app {

namespace {

enum Column { Requirement, Status, Measured, Required, Implementation, ColumnCount };

QString label(const QString& status) {
    static const std::map<QString, QString> names = {{"pass", "Verified"}, {"fail", "Fails"}, {"error", "Error"}, {"stale", "Out of date"},
                                                     {"noSpec", "No spec"}, {"notRun", "Not run"}, {"unmapped", "Not mapped"}};
    const auto it = names.find(status);
    return it == names.end() ? status : it->second;
}

QColor colourOf(const QString& status) {
    const Theme& t = Theme::current();
    if (status == "pass") return t.pass;
    if (status == "fail" || status == "error") return t.fail;
    if (status == "stale" || status == "noSpec" || status == "notRun") return t.warn;
    return t.muted;
}

QString number(double v) { return QString::number(v, 'g', 5); }

}  // namespace

Verifier::Verifier(Session& session, const QString& lib, const QString& cell, const QString& view, QWidget* parent)
    : QMainWindow(parent, Qt::Window), session_(session), lib_(lib), cell_(cell), view_(view) {
    setWindowTitle("Verifier: " + lib + " " + cell + " " + view);
    auto* split = new QSplitter(Qt::Vertical, this);
    tree_ = new QTreeWidget(split);
    tree_->setColumnCount(ColumnCount);
    tree_->setHeaderLabels({"Requirement", "Status", "Measured", "Required", "Proved by"});
    tree_->header()->setStretchLastSection(true);
    tree_->setColumnWidth(Requirement, 340);
    tree_->setColumnWidth(Status, 110);
    tree_->setColumnWidth(Measured, 190);
    tree_->setColumnWidth(Required, 130);
    derivation_ = new QPlainTextEdit(split);
    derivation_->setReadOnly(true);
    derivation_->setPlaceholderText("Select a requirement to see what proves it.");
    split->setStretchFactor(0, 4);
    split->setStretchFactor(1, 1);
    setCentralWidget(split);
    status_ = new QLabel(this);
    statusBar()->addWidget(status_, 1);

    QMenu* edit = menuBar()->addMenu("&Requirements");
    const auto act = [this](QMenu* m, const QString& text, const QKeySequence& key, auto fn) {
        QAction* a = m->addAction(text, this, fn);
        a->setShortcut(key);
    };
    act(edit, "Add Requirement…", QKeySequence("A"), [this] { addRequirement(); });
    act(edit, "Map to an Output…", QKeySequence("M"), [this] { mapRequirement(); });
    act(edit, "Remove", QKeySequence::Delete, [this] { removeRequirement(); });
    edit->addSeparator();
    act(edit, "Check", QKeySequence("F5"), [this] { check(); });
    act(edit, "Run What Is Unverified", QKeySequence("Shift+F5"), [this] { runUnverified(); });

    poll_ = new QTimer(this);
    poll_->setInterval(1000);
    connect(poll_, &QTimer::timeout, this, [this] { check(); });
    connect(tree_, &QTreeWidget::currentItemChanged, this, [this] { fillDerivation(); });
    connect(&session_, &Session::changed, this, [this] { check(); });
    connect(static_cast<ThemeNotifier*>(Theme::notifier()), &ThemeNotifier::changed, this, [this] { check(); });
    check();
}

QString Verifier::status() const { return status_->text(); }
QString Verifier::derivation() const { return derivation_->toPlainText(); }

QString Verifier::selectedId() const {
    const auto* i = tree_->currentItem();
    return i ? i->data(Requirement, Qt::UserRole).toString() : QString();
}

void Verifier::check() {
    const QString keep = selectedId();
    const cmd::Result r = session_.call("verifier_check", {{"library", lib_}, {"cell", cell_}, {"view", view_}});
    if (!r.ok) {
        status_->setText(r.error);
        return;
    }
    last_ = r.value.toObject();
    tree_->clear();
    std::map<QString, QTreeWidgetItem*> items;
    const QJsonArray reqs = last_["requirements"].toArray();
    // Parents may be listed after their children: place every requirement in a second pass over the first's items
    for (const auto& v : reqs) {
        const QJsonObject q = v.toObject();
        auto* item = new QTreeWidgetItem();
        const QString st = q["status"].toString();
        item->setText(Requirement, q["id"].toString() + "  " + q["title"].toString());
        item->setData(Requirement, Qt::UserRole, q["id"].toString());
        item->setText(Status, label(st));
        item->setForeground(Status, colourOf(st));
        QString required;
        if (q.contains("min")) required += "≥ " + number(q["min"].toDouble()) + " ";
        if (q.contains("max")) required += "≤ " + number(q["max"].toDouble()) + " ";
        if (q.contains("minYield")) required += "yield ≥ " + number(q["minYield"].toDouble());
        const QJsonArray maps = q["mappings"].toArray();
        QStringList measured, by;
        bool ownSpec = false;
        for (const auto& mv : maps) {
            const QJsonObject m = mv.toObject();
            if (m.contains("low")) measured << number(m["low"].toDouble()) + " to " + number(m["high"].toDouble());
            by << m["cell"].toString() + " " + m["test"].toString() + "." + m["output"].toString() + (m.contains("history") ? " (" + m["history"].toString() + ")" : "");
            ownSpec = true;
        }
        if (required.isEmpty() && ownSpec) required = "the output's spec";
        item->setText(Measured, measured.join("; "));
        item->setText(Required, required.trimmed());
        item->setText(Implementation, by.join("; "));
        items[q["id"].toString()] = item;
    }
    for (const auto& v : reqs) {
        const QJsonObject q = v.toObject();
        QTreeWidgetItem* item = items[q["id"].toString()];
        const auto parent = items.find(q["parent"].toString());
        if (q.contains("parent") && parent != items.end()) parent->second->addChild(item);
        else tree_->addTopLevelItem(item);
    }
    tree_->expandAll();
    if (const auto again = items.find(keep); again != items.end()) tree_->setCurrentItem(again->second);

    const int leaves = last_["leaves"].toInt(), verified = last_["verified"].toInt();
    if (!leaves) {
        status_->setText("No requirements yet. Press A to add one.");
    } else {
        QStringList rest;
        const QJsonObject counts = last_["counts"].toObject();
        for (const char* k : {"fail", "error", "stale", "noSpec", "notRun", "unmapped"})
            if (counts.contains(k)) rest << QString::number(counts[k].toInt()) + " " + label(k).toLower();
        status_->setText(QString("%1 of %2 verified").arg(verified).arg(leaves) + (rest.isEmpty() ? "" : ": " + rest.join(", ")));
    }
    fillDerivation();
}

void Verifier::fillDerivation() {
    const QString id = selectedId();
    QStringList lines;
    for (const auto& v : last_["requirements"].toArray()) {
        const QJsonObject q = v.toObject();
        if (q["id"].toString() != id) continue;
        for (const auto& m : q["mappings"].toArray()) lines << m.toObject()["derivation"].toString();
        if (lines.isEmpty()) lines << (q["status"] == "unmapped" ? "Nothing proves this yet: map it to an output (M)." : "Its status comes from the requirements under it.");
    }
    derivation_->setPlainText(lines.join("\n"));
}

int Verifier::runUnverified() {
    std::set<std::tuple<QString, QString, QString>> toRun;
    for (const auto& v : last_["requirements"].toArray())
        for (const auto& m : v.toObject()["mappings"].toArray()) {
            const QJsonObject o = m.toObject();
            const QString st = o["status"].toString();
            if (st == "notRun" || st == "stale" || st == "error") toRun.insert({o["library"].toString(), o["cell"].toString(), o["view"].toString()});
        }
    int started = 0;
    for (const auto& [l, c, w] : toRun) {
        const cmd::Result r = session_.call("ade_run", {{"library", l}, {"cell", c}, {"view", w}, {"wait", false}});
        if (r.ok) ++started;
        else status_->setText(r.error);
    }
    if (started) {
        poll_->start();
        QTimer::singleShot(600000, poll_, &QTimer::stop);  // ponytail: polls for ten minutes; a run longer than that needs F5 at its end
        status_->setText(QString("Started %1 runs").arg(started));
    }
    return started;
}

void Verifier::addRequirement() {
    const cmd::Command* c = session_.registry().find("verifier_requirement_add");
    QJsonObject prefill{{"library", lib_}, {"cell", cell_}, {"view", view_}};
    if (!selectedId().isEmpty()) prefill["parent"] = selectedId();
    CommandForm form(*c, prefill, this);
    if (form.exec() != QDialog::Accepted) return;
    QJsonObject args;
    QString error;
    if (!form.arguments(&args, &error)) return status_->setText(error);
    const cmd::Result r = session_.call(c->name, args);
    if (!r.ok) status_->setText(r.error);
}

void Verifier::mapRequirement() {
    if (selectedId().isEmpty()) return status_->setText("Select a requirement first");
    const cmd::Command* c = session_.registry().find("verifier_map");
    CommandForm form(*c, {{"library", lib_}, {"cell", cell_}, {"view", view_}, {"id", selectedId()}, {"mapLibrary", lib_}, {"mapCell", cell_}}, this);
    if (form.exec() != QDialog::Accepted) return;
    QJsonObject args;
    QString error;
    if (!form.arguments(&args, &error)) return status_->setText(error);
    const cmd::Result r = session_.call(c->name, args);
    if (!r.ok) status_->setText(r.error);
}

void Verifier::removeRequirement() {
    if (selectedId().isEmpty()) return status_->setText("Select a requirement first");
    const cmd::Result r = session_.call("verifier_requirement_remove", {{"library", lib_}, {"cell", cell_}, {"view", view_}, {"id", selectedId()}});
    if (!r.ok) status_->setText(r.error);
}

}  // namespace app
