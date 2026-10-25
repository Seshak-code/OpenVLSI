#include "app/library_manager.h"

#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QToolBar>
#include <QVBoxLayout>

namespace app {

namespace {
QListWidget* column(QHBoxLayout* row, const QString& title, QWidget* parent) {
    auto* box = new QVBoxLayout;
    box->addWidget(new QLabel("<b>" + title + "</b>", parent));
    auto* list = new QListWidget(parent);
    box->addWidget(list);
    row->addLayout(box);
    return list;
}
}  // namespace

LibraryManager::LibraryManager(Session& session, QWidget* parent) : QWidget(parent, Qt::Window), session_(session) {
    setWindowTitle("Library Manager");
    auto* outer = new QVBoxLayout(this);
    auto* bar = new QToolBar(this);
    outer->addWidget(bar);
    auto* row = new QHBoxLayout;
    libs_ = column(row, "Library", this);
    cells_ = column(row, "Cell", this);
    views_ = column(row, "View", this);
    outer->addLayout(row);

    // the dialogs gather names; the actions run the commands
    bar->addAction("New Library…", this, [this] {
        const QString name = QInputDialog::getText(this, "New Library", "Library name:");
        if (name.isEmpty()) return;
        const QString dir = QFileInfo(session_.workspaceFile()).absolutePath();
        const QString path = QFileDialog::getSaveFileName(this, "Library file", dir + "/" + name + ".oalib", "Libraries (*.oalib)");
        if (!path.isEmpty()) createLibrary(name, path);
    });
    bar->addAction("Define Library…", this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, "Library file", {}, "Libraries (*.oalib)");
        if (path.isEmpty()) return;
        const QString name = QInputDialog::getText(this, "Define Library", "Library name:", QLineEdit::Normal, QFileInfo(path).baseName());
        if (!name.isEmpty()) defineLibrary(name, path);
    });
    bar->addSeparator();
    bar->addAction("New Cell…", this, [this] {
        const QString name = QInputDialog::getText(this, "New Cell", "Cell name in " + selectedLibrary() + ":");
        if (!name.isEmpty()) createCell(name);
    });
    bar->addAction("New View…", this, [this] {
        const QString name = QInputDialog::getText(this, "New View", "View name:", QLineEdit::Normal, "schematic");
        if (name.isEmpty()) return;
        bool ok = false;
        const QString type = QInputDialog::getItem(this, "New View", "Type:", {"schematic", "symbol"}, 0, false, &ok);
        if (ok) createView(name, type);
    });
    bar->addAction("Rename…", this, [this] {
        const QString to = QInputDialog::getText(this, "Rename Cell", "New name for " + selectedCell() + ":", QLineEdit::Normal, selectedCell());
        if (!to.isEmpty() && to != selectedCell()) renameCell(to);
    });
    bar->addAction("Delete", this, [this] {
        const QString what = selectedView().isEmpty() ? "cell " + selectedCell() : "view " + selectedCell() + "/" + selectedView();
        if (QMessageBox::question(this, "Delete", "Delete " + what + "? (Undo brings it back)") == QMessageBox::Yes) deleteSelected();
    });
    bar->addSeparator();
    bar->addAction("Undo", this, [this] { undo(); });
    bar->addAction("Redo", this, [this] { redo(); });

    connect(libs_, &QListWidget::currentTextChanged, this, [this] { if (!filling_) fillCells(); });
    connect(cells_, &QListWidget::currentTextChanged, this, [this] { if (!filling_) fillViews(); });
    connect(views_, &QListWidget::itemDoubleClicked, this, [this] { openSelected(); });
    connect(&session_, &Session::changed, this, &LibraryManager::refresh);
    resize(640, 420);
    refresh();
}

void LibraryManager::changeEvent(QEvent* e) {
    if (e->type() == QEvent::ActivationChange && isActiveWindow()) refresh();  // another process may have written
    QWidget::changeEvent(e);
}

QStringList LibraryManager::items(const QListWidget* w) {
    QStringList out;
    for (int i = 0; i < w->count(); ++i) out << w->item(i)->text();
    return out;
}

QString LibraryManager::current(const QListWidget* w) { return w->currentItem() ? w->currentItem()->text() : QString(); }

QString LibraryManager::selectedLibrary() const { return current(libs_); }
QString LibraryManager::selectedCell() const { return current(cells_); }
QString LibraryManager::selectedView() const {
    const QString v = current(views_);
    return v.left(v.indexOf(" ("));
}
QStringList LibraryManager::libraries() const { return items(libs_); }
QStringList LibraryManager::cells() const { return items(cells_); }
QStringList LibraryManager::views() const { return items(views_); }

// One list refilled from a command's result, keeping the selection when the entry is still there
static void fill(QListWidget* w, const QStringList& entries, const QString& keep) {
    w->clear();
    w->addItems(entries);
    const auto found = w->findItems(keep, Qt::MatchExactly);
    if (!found.isEmpty()) w->setCurrentItem(found.first());
}

void LibraryManager::refresh() {
    const QString lib = selectedLibrary();
    QStringList names;
    for (const auto& v : session_.call("workspace_libraries").value.toArray()) names << v.toString();
    filling_ = true;
    fill(libs_, names, lib);
    filling_ = false;
    fillCells();
}

void LibraryManager::fillCells() {
    const QString keep = selectedCell();
    QStringList names;
    if (!selectedLibrary().isEmpty())
        for (const auto& v : session_.call("cell_list", {{"library", selectedLibrary()}}).value.toArray()) names << v.toString();
    filling_ = true;
    fill(cells_, names, keep);
    filling_ = false;
    fillViews();
}

void LibraryManager::fillViews() {
    const QString keep = current(views_);
    QStringList names;
    if (!selectedCell().isEmpty())
        for (const auto& v : session_.call("view_list", {{"library", selectedLibrary()}, {"cell", selectedCell()}}).value.toArray())
            names << v.toObject()["view"].toString() + " (" + v.toObject()["type"].toString() + ")";
    filling_ = true;
    fill(views_, names, keep);
    filling_ = false;
}

void LibraryManager::select(const QString& library, const QString& cell, const QString& view) {
    auto pick = [](QListWidget* w, const QString& text, bool prefix) {
        for (int i = 0; i < w->count(); ++i)
            if (prefix ? w->item(i)->text().startsWith(text + " (") : w->item(i)->text() == text) { w->setCurrentRow(i); return; }
        w->setCurrentItem(nullptr);
    };
    pick(libs_, library, false);
    pick(cells_, cell, false);
    pick(views_, view, true);
}

bool LibraryManager::createLibrary(const QString& name, const QString& path) {
    const bool ok = session_.call("library_create", {{"library", name}, {"path", path}}).ok;
    if (ok) select(name);
    return ok;
}

bool LibraryManager::defineLibrary(const QString& name, const QString& path) {
    const bool ok = session_.call("library_define", {{"library", name}, {"path", path}}).ok;
    if (ok) select(name);
    return ok;
}

bool LibraryManager::createCell(const QString& name) {
    const QString lib = selectedLibrary();
    const bool ok = session_.call("cell_create", {{"library", lib}, {"cell", name}}).ok;
    if (ok) select(lib, name);
    return ok;
}

bool LibraryManager::createView(const QString& name, const QString& type) {
    const QString lib = selectedLibrary(), cell = selectedCell();
    const bool ok = session_.call("view_create", {{"library", lib}, {"cell", cell}, {"view", name}, {"type", type}}).ok;
    if (ok) select(lib, cell, name);
    return ok;
}

bool LibraryManager::renameCell(const QString& to) {
    const QString lib = selectedLibrary();
    const bool ok = session_.call("cell_rename", {{"library", lib}, {"cell", selectedCell()}, {"to", to}}).ok;
    if (ok) select(lib, to);
    return ok;
}

bool LibraryManager::deleteSelected() {
    const QString lib = selectedLibrary(), cell = selectedCell(), view = selectedView();
    if (!view.isEmpty()) return session_.call("view_delete", {{"library", lib}, {"cell", cell}, {"view", view}}).ok;
    return session_.call("cell_delete", {{"library", lib}, {"cell", cell}}).ok;
}

bool LibraryManager::openSelected() {
    return session_.call("editor_open", {{"library", selectedLibrary()}, {"cell", selectedCell()}, {"view", selectedView()}}).ok;
}

bool LibraryManager::undo() { return session_.call("edit_undo", {{"library", selectedLibrary()}}).ok; }
bool LibraryManager::redo() { return session_.call("edit_redo", {{"library", selectedLibrary()}}).ok; }

}  // namespace app
