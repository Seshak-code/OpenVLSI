#include "app/verify_stage.h"

#include "app/icons.h"
#include "app/theme.h"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <map>

namespace app {

VerifyPanel::VerifyPanel(Session& session, Actions actions, QWidget* parent) : QWidget(parent), session_(session), actions_(std::move(actions)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* row = new QHBoxLayout();
    row->setSpacing(0);
    auto* drc = new QPushButton("Run DRC…", this);
    auto* lvs = new QPushButton("Run LVS…", this);
    connect(drc, &QPushButton::clicked, this, [this] { if (actions_.form) actions_.form("verify_drc"); });
    connect(lvs, &QPushButton::clicked, this, [this] { if (actions_.form) actions_.form("verify_lvs"); });
    row->addWidget(drc);
    row->addWidget(lvs);
    layout->addLayout(row);
    tree_ = new QTreeWidget(this);
    tree_->setHeaderHidden(true);
    tree_->setAccessibleName("Verification runs");
    layout->addWidget(tree_, 1);
    details_ = new QLabel(this);
    details_->setWordWrap(true);
    details_->setContentsMargins(8, 6, 8, 6);
    details_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(details_);
    const auto activated = [this](QTreeWidgetItem* i) {
        const QString run = i->data(0, Qt::UserRole).toString();
        if (!run.isEmpty()) open(run);
    };
    connect(tree_, &QTreeWidget::itemActivated, this, activated);
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, activated);
    poll_ = new QTimer(this);
    poll_->setInterval(2000);
    connect(poll_, &QTimer::timeout, this, [this] { if (isVisible()) refresh(); });
    poll_->start();
    refresh();
}

QString VerifyPanel::details() const { return details_->text(); }

void VerifyPanel::refresh() {
    const cmd::Result r = session_.call("verify_list");
    if (!r.ok) return;
    const QString keep = tree_->currentItem() ? tree_->currentItem()->data(0, Qt::UserRole).toString() : QString();
    const Theme& t = Theme::current();
    std::map<QString, QTreeWidgetItem*> groups;
    tree_->clear();
    for (const auto& v : r.value.toArray()) {
        const QJsonObject o = v.toObject();
        const QString run = o["run"].toString();
        const QString group = o.contains("cell") ? o["library"].toString() + " " + o["cell"].toString() : run.section('/', 0, 0);
        auto [it, fresh] = groups.try_emplace(group, nullptr);
        if (fresh) {
            it->second = new QTreeWidgetItem(tree_, {group});
            it->second->setExpanded(true);
        }
        const QString state = o["state"].toString();
        QString result;
        QColor colour = t.fail;
        if (state == "running") {
            result = "running";
            colour = t.via;
        } else if (o.contains("error")) {
            result = "did not finish";
        } else if (o["clean"].toBool()) {
            result = o["kind"] == "lvs" ? "matched" : "clean";
            colour = t.pass;
        } else {
            result = o["kind"] == "lvs" ? "does not match" : QString::number(o["violations"].toInt()) + " violations";
        }
        auto* item = new QTreeWidgetItem(it->second, {run.section('/', 1) + "  " + result});
        item->setData(0, Qt::UserRole, run);
        item->setForeground(0, colour);
        if (run == keep) tree_->setCurrentItem(item);
    }
}

bool VerifyPanel::open(const QString& run) {
    const cmd::Result st = session_.call("verify_status", {{"run", run}});
    if (!st.ok) {
        details_->setText(st.error);
        return false;
    }
    const QJsonObject o = st.value.toObject();
    if (o["state"] == "running") {
        details_->setText("Still running");
        return false;
    }
    if (o.contains("error")) {
        details_->setText(o["error"].toString());
        return false;
    }
    if (o["kind"] == "drc") {
        const cmd::Result shown = session_.call("layout_view_markers", {{"library", o["library"]}, {"cell", o["cell"]}, {"view", o["view"]}, {"run", run}});
        details_->setText(shown.ok ? (o["clean"].toBool() ? "No violations" : QString::number(o["violations"].toInt()) + " violations, marked on the layout") : shown.error);
        return shown.ok;
    }
    details_->setText(QString(o["matched"].toBool() ? "Layout and schematic match" : "Layout and schematic differ") +
                      QString(": %1 devices in the layout, %2 in the schematic; %3 nets, %4; %5 mismatches.")
                          .arg(o["devices_layout"].toInt()).arg(o["devices_schematic"].toInt()).arg(o["nets_layout"].toInt()).arg(o["nets_schematic"].toInt()).arg(o["mismatches"].toInt()));
    return true;
}

}  // namespace app
