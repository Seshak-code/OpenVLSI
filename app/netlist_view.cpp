#include "app/netlist_view.h"

#include "app/theme.h"

#include <QComboBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

namespace app {

namespace {
const char* kByList = "(view list)";

QTableWidgetItem* item(const QString& text, bool right = false) {
    auto* made = new QTableWidgetItem(text);
    if (right) made->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return made;
}
}  // namespace

NetlistView::NetlistView(Session& session, const QString& lib, const QString& cellName, const QString& view, QWidget* parent)
    : QMainWindow(parent, Qt::Window), session_(session), lib_(lib), cell_(cellName), view_(view) {
    config_ = session_.call("view_list", {{"library", lib_}, {"cell", cell_}}).value.toArray().contains(QJsonObject{{"type", "config"}, {"view", view_}});
    setWindowTitle(cell_ + " " + view_);
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(14, 10, 14, 6);
    layout->setSpacing(8);
    if (config_) {
        auto* row = new QHBoxLayout();
        top_ = new QLineEdit(central);
        top_->setAccessibleName("Top");
        top_->setPlaceholderText("library/cell/view of the schematic to netlist");
        list_ = new QLineEdit(central);
        list_->setAccessibleName("View list");
        list_->setPlaceholderText("views in the order tried, as: extracted schematic spice");
        auto* apply = new QPushButton("Apply", central);
        auto* netlist = new QPushButton("Netlist", central);
        apply->setProperty("api", "config_set");
        netlist->setProperty("api", "netlist");
        row->addWidget(new QLabel("Top", central));
        row->addWidget(top_, 2);
        row->addWidget(new QLabel("View list", central));
        row->addWidget(list_, 2);
        row->addWidget(apply);
        row->addWidget(netlist);
        layout->addLayout(row);
        connect(apply, &QPushButton::clicked, this, [this] { this->apply(); });
        connect(top_, &QLineEdit::returnPressed, this, [this] { this->apply(); });
        connect(list_, &QLineEdit::returnPressed, this, [this] { this->apply(); });
        connect(netlist, &QPushButton::clicked, this, [this] { showNetlist(); });
    }
    status_ = new QLabel(central);
    status_->setWordWrap(true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    status_->setAccessibleName("Status");
    layout->addWidget(status_);
    auto* split = new QSplitter(Qt::Vertical, central);
    table_ = new QTableWidget(split);
    table_->setAccessibleName(config_ ? "Cells" : "Nets");
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->verticalHeader()->setVisible(false);
    table_->verticalHeader()->setDefaultSectionSize(26);
    table_->horizontalHeader()->setStretchLastSection(true);
    text_ = new QPlainTextEdit(split);
    text_->setReadOnly(true);
    text_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    text_->setAccessibleName("Netlist");
    text_->setPlaceholderText(config_ ? "Netlist shows what this config simulates" : QString());
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    layout->addWidget(split, 1);
    setCentralWidget(central);
    connect(&session_, &Session::changed, this, [this] { refresh(); });
    connect(static_cast<ThemeNotifier*>(Theme::notifier()), &ThemeNotifier::changed, this, [this] { refresh(); });
    refresh();
}

QString NetlistView::status() const { return status_->text(); }
QString NetlistView::text() const { return text_->toPlainText(); }

void NetlistView::refresh() {
    const Theme& t = Theme::current();
    const QJsonObject which{{"library", lib_}, {"cell", cell_}, {"view", view_}};
    table_->clearContents();
    if (!config_) {
        // An extraction: what it measured, and what it wrote
        QJsonObject key = which;
        key["key"] = "extraction";
        const QJsonObject x = session_.call("prop_get", key).value.toObject();
        key["key"] = "netlist";
        text_->setPlainText(session_.call("prop_get", key).value.toString());
        const QJsonArray metals = x["metals"].toArray(), nets = x["nets"].toArray();
        QStringList heads{"Net", "Wiring (fF)"};
        for (const auto& m : metals) heads << m.toString() + " (fF)";
        table_->setColumnCount((int)heads.size());
        table_->setHorizontalHeaderLabels(heads);
        table_->setRowCount((int)nets.size());
        for (int k = 0; k < (int)nets.size(); ++k) {
            const QJsonObject n = nets[k].toObject();
            table_->setItem(k, 0, item(n["name"].toString()));
            table_->setItem(k, 1, item(QString::number(n["c_ff"].toDouble(), 'f', 3), true));
            for (int m = 0; m < (int)metals.size(); ++m) {
                const QJsonObject per = n["layers"].toObject()[metals[m].toString()].toObject();
                table_->setItem(k, 2 + m, item(per.isEmpty() ? QString() : QString::number(per["c_ff"].toDouble(), 'f', 3), true));
            }
        }
        table_->resizeColumnsToContents();
        status_->setText(x.isEmpty() ? "This view holds no extraction. Run Extract on a matched LVS (Verify)."
                                     : QString("Extracted from %1: %2 devices as drawn, %3 fF of wiring. %4.").arg(x["run"].toString()).arg(x["devices"].toInt()).arg(x["total_ff"].toDouble(), 0, 'f', 3).arg(x["method"].toString()));
        status_->setStyleSheet({});
        return;
    }
    const cmd::Result r = session_.call("config_get", which);
    if (!r.ok) {
        status_->setText(r.error);
        status_->setStyleSheet(QString("color: %1;").arg(t.fail.name()));
        table_->setRowCount(0);
        return;
    }
    last_ = r.value.toObject();
    QStringList list;
    for (const auto& v : last_["viewList"].toArray()) list << v.toString();
    if (!top_->hasFocus()) top_->setText(last_["top"].toString());
    if (!list_->hasFocus()) list_->setText(list.join(' '));
    const QJsonArray cells = last_["cells"].toArray();
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels({"Cell", "Placed", "Uses", "Bind to", "Its views"});
    table_->setRowCount((int)cells.size());
    int unresolved = 0;
    for (int k = 0; k < (int)cells.size(); ++k) {
        const QJsonObject c = cells[k].toObject();
        QStringList views;
        for (const auto& v : c["views"].toArray()) views << v.toString();
        table_->setItem(k, 0, item(c["library"].toString() + "/" + c["cell"].toString()));
        table_->setItem(k, 1, item(QString::number(c["placed"].toInt()), true));
        auto* uses = item(c.contains("error") ? "none" : c["uses"].toString());
        if (c.contains("error")) {
            uses->setForeground(t.fail);
            uses->setToolTip(c["error"].toString());
            ++unresolved;
        }
        table_->setItem(k, 2, uses);
        auto* bind = new QComboBox(table_);
        bind->setProperty("api", "config_set");
        bind->setAccessibleName("Bind " + c["cell"].toString());
        bind->addItem(kByList);
        bind->addItems(views);
        const QString bound = last_["bindings"].toObject()[c["library"].toString() + "/" + c["cell"].toString()].toString();
        if (!bound.isEmpty() && !views.contains(bound)) bind->addItem(bound);  // a binding to a view it lacks: shown, so that it can be undone
        bind->setCurrentText(bound.isEmpty() ? QString(kByList) : bound);
        connect(bind, &QComboBox::activated, this, [this, k, bind] { this->bind(k, bind->currentText() == kByList ? QString() : bind->currentText()); });
        table_->setCellWidget(k, 3, bind);
        table_->setItem(k, 4, item(views.join(", ")));
    }
    table_->resizeColumnsToContents();
    if (last_["top"].toString().isEmpty()) {
        // A config just made: its own cell's schematic is the likely top
        if (!top_->hasFocus() && top_->text().isEmpty() && session_.call("view_list", {{"library", lib_}, {"cell", cell_}}).value.toArray().contains(QJsonObject{{"type", "schematic"}, {"view", "schematic"}}))
            top_->setText(lib_ + "/" + cell_ + "/schematic");
        if (!list_->hasFocus() && list.isEmpty()) list_->setText("spice schematic");
        status_->setText("This config names no top yet. Set the schematic it starts from and its view list, then Apply.");
        status_->setStyleSheet(QString("color: %1;").arg(t.warn.name()));
    } else {
        status_->setText(unresolved ? QString("%1 of %2 cells get no view: a netlist of this config fails. Change the view list, or bind them.").arg(unresolved).arg(cells.size())
                                    : QString("%1 cells, each with a view. Bind to puts one cell's view ahead of the list.").arg(cells.size()));
        status_->setStyleSheet(QString("color: %1;").arg((unresolved ? t.fail : t.pass).name()));
    }
}

bool NetlistView::apply() {
    if (!config_) return false;
    QJsonArray list;
    for (const QString& v : list_->text().split(' ', Qt::SkipEmptyParts)) list.append(v);
    top_->clearFocus();  // so that what was written shows as the database holds it
    list_->clearFocus();
    const cmd::Result r = session_.call("config_set", {{"library", lib_}, {"cell", cell_}, {"view", view_}, {"top", top_->text().trimmed()}, {"viewList", list}});
    if (!r.ok) {
        status_->setText(r.error);
        status_->setStyleSheet(QString("color: %1;").arg(Theme::current().fail.name()));
    }
    return r.ok;
}

bool NetlistView::bind(int row, const QString& view) {
    const QJsonArray cells = last_["cells"].toArray();
    if (!config_ || row < 0 || row >= cells.size()) return false;
    QJsonObject bindings = last_["bindings"].toObject();
    const QString key = cells[row].toObject()["library"].toString() + "/" + cells[row].toObject()["cell"].toString();
    if (view.isEmpty()) bindings.remove(key);
    else bindings[key] = view;
    const cmd::Result r = session_.call("config_set", {{"library", lib_}, {"cell", cell_}, {"view", view_}, {"bindings", bindings}});
    if (!r.ok) {
        status_->setText(r.error);
        status_->setStyleSheet(QString("color: %1;").arg(Theme::current().fail.name()));
    }
    return r.ok;
}

bool NetlistView::showNetlist() {
    const cmd::Result r = session_.call("netlist", {{"library", lib_}, {"cell", cell_}, {"view", view_}});
    text_->setPlainText(r.ok ? r.value.toString() : r.error);
    return r.ok;
}

}  // namespace app
