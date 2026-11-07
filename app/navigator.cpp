#include "app/navigator.h"

#include "app/icons.h"
#include "app/theme.h"

#include <QHeaderView>
#include <QJsonArray>
#include <QLineEdit>
#include <QMenu>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace app {

namespace {
enum Role { kLib = Qt::UserRole, kCell, kView, kHistory };

QString pathOf(const QTreeWidgetItem* i) {
    return i->data(0, kLib).toString() + "/" + i->data(0, kCell).toString() + "/" + i->data(0, kView).toString() + "/" + i->data(0, kHistory).toString();
}

QTreeWidgetItem* node(QTreeWidgetItem* parent, QTreeWidget* tree, const QString& text, const QString& icon, const QString& lib,
                      const QString& cell = {}, const QString& view = {}, const QString& history = {}) {
    auto* i = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
    i->setText(0, text);
    i->setIcon(0, icons::get(icon));
    i->setData(0, kLib, lib);
    i->setData(0, kCell, cell);
    i->setData(0, kView, view);
    i->setData(0, kHistory, history);
    return i;
}
}  // namespace

Navigator::Navigator(Session& session, Mode mode, Actions actions, QWidget* parent)
    : QWidget(parent), session_(session), mode_(mode), actions_(std::move(actions)) {
    setObjectName("navigator");
    setAttribute(Qt::WA_StyledBackground);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    filter_ = new QLineEdit(this);
    filter_->setPlaceholderText(mode == Mode::Design ? "Filter libraries, cells and views" : "Filter simulations");
    filter_->setClearButtonEnabled(true);
    tree_ = new QTreeWidget(this);
    tree_->setHeaderHidden(true);
    tree_->setIconSize({16, 16});
    tree_->setIndentation(14);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(filter_);
    layout->addWidget(tree_, 1);
    connect(filter_, &QLineEdit::textChanged, this, [this] { applyFilter(); });
    connect(tree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* i) {
        if (!i->data(0, kView).toString().isEmpty())
            actions_.open(i->data(0, kLib).toString(), i->data(0, kCell).toString(), i->data(0, kView).toString(), i->data(0, kHistory).toString());
    });
    connect(tree_, &QWidget::customContextMenuRequested, this, &Navigator::contextMenu);
    connect(&session_, &Session::changed, this, &Navigator::refresh);
    connect(Theme::notifier(), SIGNAL(changed()), this, SLOT(refresh()));
    refresh();
}

void Navigator::setFilter(const QString& text) { filter_->setText(text); }

bool Navigator::select(const QString& library, const QString& cell, const QString& view) {
    QStringList want{library};
    if (!cell.isEmpty()) want << cell;
    if (!view.isEmpty()) want << view;
    const std::function<QTreeWidgetItem*(QTreeWidgetItem*, int)> find = [&](QTreeWidgetItem* at, int depth) -> QTreeWidgetItem* {
        for (int k = 0; k < (at ? at->childCount() : tree_->topLevelItemCount()); ++k) {
            QTreeWidgetItem* i = at ? at->child(k) : tree_->topLevelItem(k);
            if (i->text(0) != want[depth]) continue;
            if (depth + 1 == want.size()) return i;
            if (QTreeWidgetItem* below = find(i, depth + 1)) return below;
        }
        return nullptr;
    };
    QTreeWidgetItem* item = find(nullptr, 0);
    if (!item) return false;
    for (QTreeWidgetItem* up = item->parent(); up; up = up->parent()) up->setExpanded(true);
    tree_->setCurrentItem(item);
    return true;
}

QJsonObject Navigator::selection() const {
    QJsonObject o;
    if (const auto* i = tree_->currentItem()) {
        for (const auto& [key, role] : {std::pair{"library", kLib}, {"cell", kCell}, {"view", kView}})
            if (const QString v = i->data(0, role).toString(); !v.isEmpty()) o[key] = v;
    }
    return o;
}

void Navigator::refresh() {
    // Keep what is expanded and selected across the rebuild
    QSet<QString> open;
    QString current = tree_->currentItem() ? pathOf(tree_->currentItem()) : QString();
    std::function<void(QTreeWidgetItem*)> walk = [&](QTreeWidgetItem* i) {
        if (i->isExpanded()) open.insert(pathOf(i));
        for (int k = 0; k < i->childCount(); ++k) walk(i->child(k));
    };
    for (int k = 0; k < tree_->topLevelItemCount(); ++k) walk(tree_->topLevelItem(k));
    const bool first = tree_->topLevelItemCount() == 0;

    tree_->clear();
    for (const auto& l : session_.call("workspace_libraries").value.toArray()) {
        const QString lib = l.toString();
        QTreeWidgetItem* libItem = mode_ == Mode::Design ? node(nullptr, tree_, lib, "library", lib) : nullptr;
        for (const auto& c : session_.call("cell_list", {{"library", lib}}).value.toArray()) {
            const QString cell = c.toString();
            QTreeWidgetItem* cellItem = mode_ == Mode::Design ? node(libItem, tree_, cell, "cell", lib, cell) : nullptr;
            for (const auto& v : session_.call("view_list", {{"library", lib}, {"cell", cell}}).value.toArray()) {
                const QString view = v.toObject()["view"].toString(), type = v.toObject()["type"].toString();
                if (mode_ == Mode::Design) {
                    node(cellItem, tree_, view, type, lib, cell, view);
                } else if (type == "maestro" || type == "verifier") {
                    auto* item = node(nullptr, tree_, cell + " " + view, type, lib, cell, view);
                    item->setToolTip(0, lib + "/" + cell + "/" + view);
                    for (const auto& h : session_.call("ade_history_list", {{"library", lib}, {"cell", cell}, {"view", view}}).value.toArray())
                        node(item, tree_, h.toString(), "waveform", lib, cell, view, h.toString());
                }
            }
        }
    }
    std::function<void(QTreeWidgetItem*)> restore = [&](QTreeWidgetItem* i) {
        const QString p = pathOf(i);
        if (open.contains(p) || (first && mode_ == Mode::Design && i->parent() == nullptr)) i->setExpanded(true);
        if (p == current) tree_->setCurrentItem(i);
        for (int k = 0; k < i->childCount(); ++k) restore(i->child(k));
    };
    for (int k = 0; k < tree_->topLevelItemCount(); ++k) restore(tree_->topLevelItem(k));
    applyFilter();
}

void Navigator::applyFilter() {
    const QString q = filter_->text().trimmed();
    std::function<bool(QTreeWidgetItem*)> apply = [&](QTreeWidgetItem* i) {
        bool any = q.isEmpty() || i->text(0).contains(q, Qt::CaseInsensitive);
        const bool self = any;
        for (int k = 0; k < i->childCount(); ++k) {
            // a matching cell shows all its views; otherwise only matching children stay
            const bool child = apply(i->child(k));
            if (self) i->child(k)->setHidden(false);
            any = any || child;
        }
        i->setHidden(!any);
        if (!q.isEmpty() && any && !self) i->setExpanded(true);
        return any;
    };
    for (int k = 0; k < tree_->topLevelItemCount(); ++k) apply(tree_->topLevelItem(k));
}

void Navigator::contextMenu(const QPoint& at) {
    QMenu menu(this);
    const QJsonObject s = [&] {
        if (auto* i = tree_->itemAt(at)) tree_->setCurrentItem(i);
        else tree_->setCurrentItem(nullptr);
        return selection();
    }();
    const bool hasLib = s.contains("library"), hasCell = s.contains("cell"), hasView = s.contains("view");
    if (mode_ == Mode::Design) {
        menu.addAction("Create library…", this, [this] { actions_.form("library_create", {}); });
        if (hasLib) menu.addAction("Create cell…", this, [this, s] { actions_.form("cell_create", s); });
        if (hasCell) menu.addAction("Create view…", this, [this, s] { actions_.form("view_create", s); });
        if (hasCell && !hasView) {
            menu.addSeparator();
            menu.addAction("Rename cell…", this, [this, s] { actions_.form("cell_rename", s); });
            menu.addAction("Delete cell…", this, [this, s] { actions_.form("cell_delete", s); });
        }
        if (hasView) {
            menu.addSeparator();
            menu.addAction("Delete view…", this, [this, s] { actions_.form("view_delete", s); });
        }
    }
    if (hasView) {
        const QString lib = s["library"].toString(), cell = s["cell"].toString(), view = s["view"].toString();
        menu.insertAction(menu.actions().value(0), menu.addAction("Open", this, [this, lib, cell, view] { actions_.open(lib, cell, view, {}); }));
    }
    if (!menu.isEmpty()) menu.exec(tree_->viewport()->mapToGlobal(at));
}

}  // namespace app
