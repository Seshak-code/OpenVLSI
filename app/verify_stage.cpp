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
    drc->setProperty("api", "verify_drc");
    lvs->setProperty("api", "verify_lvs");
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
    QString text = QString(o["matched"].toBool() ? "Layout and schematic match" : "Layout and schematic differ") +
                   QString(": %1 devices in the layout, %2 in the schematic; %3 nets, %4; %5 mismatches.")
                       .arg(o["devices_layout"].toInt()).arg(o["devices_schematic"].toInt()).arg(o["nets_layout"].toInt()).arg(o["nets_schematic"].toInt()).arg(o["mismatches"].toInt());
    if (o["matched"].toBool()) {
        details_->setText(text);
        return true;
    }
    // What differs, in words, and on the layout
    const cmd::Result report = session_.call("verify_lvs_report", {{"run", run}});
    if (report.ok) {
        const auto side = [](const QJsonValue& v) { return v.isNull() ? QString("(none)") : v.toString(); };
        int shown = 0;
        for (const auto& cv : report.value.toObject()["circuits"].toArray())
            for (const auto& iv : cv.toObject()["items"].toArray()) {
                const QJsonObject it = iv.toObject();
                if (it["kind"] == "pin" || it["status"] == "warning" || ++shown > 12) continue;
                QStringList layout, schematic;
                for (const auto& cn : it["connections"].toArray()) {
                    if (!cn.toArray()[0].isNull()) layout << cn.toArray()[0].toString();
                    if (!cn.toArray()[1].isNull()) schematic << cn.toArray()[1].toString();
                }
                text += QString("\n%1 %2 / %3").arg(it["kind"].toString(), side(it["layout"]), side(it["schematic"]));
                if (it["kind"] == "net") text += QString(": joins %1 in the layout, %2 in the schematic").arg(layout.isEmpty() ? "nothing" : layout.join(", "), schematic.isEmpty() ? "nothing" : schematic.join(", "));
            }
        if (shown > 12) text += QString("\nand %1 more, on the layout").arg(shown - 12);
    }
    const cmd::Result shown = session_.call("layout_view_markers", {{"library", o["library"]}, {"cell", o["cell"]}, {"view", o["view"]}, {"run", run}});
    details_->setText(shown.ok ? text : text + "\n" + shown.error);
    return shown.ok;
}

}  // namespace app
