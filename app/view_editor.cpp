#include "app/view_editor.h"

#include "app/canvas.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>

#include <cmath>

namespace app {

namespace {

QJsonArray xy(QPointF p) { return QJsonArray{std::round(p.x()), std::round(p.y())}; }

const QStringList kTurns = {"R0", "R90", "R180", "R270"};
QString nextTurn(const QString& o) {  // a quarter turn more; a mirrored orientation turns within its own four
    static const QStringList mirrored = {"MY", "MYR90", "MX", "MXR90"};
    if (const int k = kTurns.indexOf(o); k >= 0) return kTurns[(k + 1) % 4];
    return mirrored[(mirrored.indexOf(o) + 1) % 4];
}

// Asks for a master symbol: library, cell, and the cell's symbol views
bool pickMaster(QWidget* parent, Session& s, QString& lib, QString& cell, QString& view) {
    QDialog d(parent);
    d.setWindowTitle("Add Instance");
    auto* form = new QFormLayout(&d);
    auto *libs = new QComboBox(&d), *cells = new QComboBox(&d), *views = new QComboBox(&d);
    form->addRow("Library", libs);
    form->addRow("Cell", cells);
    form->addRow("View", views);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    auto fillViews = [&] {
        views->clear();
        for (const auto& v : s.call("view_list", {{"library", libs->currentText()}, {"cell", cells->currentText()}}).value.toArray())
            if (v.toObject()["type"] == "symbol") views->addItem(v.toObject()["view"].toString());
    };
    auto fillCells = [&] {
        cells->clear();
        for (const auto& c : s.call("cell_list", {{"library", libs->currentText()}}).value.toArray()) cells->addItem(c.toString());
        fillViews();
    };
    for (const auto& l : s.call("workspace_libraries").value.toArray()) libs->addItem(l.toString());
    QObject::connect(libs, &QComboBox::currentTextChanged, &d, fillCells);
    QObject::connect(cells, &QComboBox::currentTextChanged, &d, fillViews);
    libs->setCurrentText(lib);
    fillCells();
    if (d.exec() != QDialog::Accepted || views->currentText().isEmpty()) return false;
    lib = libs->currentText(), cell = cells->currentText(), view = views->currentText();
    return true;
}

}  // namespace

ViewEditor::ViewEditor(Session& session, const QString& lib, const QString& cell, const QString& view, QWidget* parent)
    : QMainWindow(parent, Qt::Window), session_(session), lib_(lib), cell_(cell), view_(view), canvas_(new Canvas(this)),
      status_(new QLabel(this)) {
    setAttribute(Qt::WA_DeleteOnClose);
    setCentralWidget(canvas_);
    statusBar()->addWidget(status_, 1);
    canvas_->setGrid(kSnap);
    connect(canvas_, &Canvas::pressed, this, [this](QPointF p, Qt::KeyboardModifiers m) { press(p, m); });
    connect(canvas_, &Canvas::moved, this, &ViewEditor::moveTo);
    connect(canvas_, &Canvas::released, this, &ViewEditor::release);
    connect(canvas_, &Canvas::doubleClicked, this, &ViewEditor::doubleClick);
    connect(&session_, &Session::changed, this, &ViewEditor::reload);
    reload();
    setWindowTitle((isSchematic() ? "Schematic Editing: " : "Symbol Editing: ") + lib + " " + cell + " " + view);
    buildMenus();
    resize(1000, 700);
    QTimer::singleShot(0, this, &ViewEditor::fit);  // once the window is laid out
}

void ViewEditor::buildMenus() {
    auto act = [this](QMenu* m, const QString& text, const QKeySequence& key, auto fn) {
        QAction* a = m->addAction(text, this, fn);
        a->setShortcut(key);
        a->setShortcutContext(Qt::WindowShortcut);
        return a;
    };
    QMenu* edit = menuBar()->addMenu("&Edit");
    act(edit, "Undo", QKeySequence("U"), [this] { undo(); });
    act(edit, "Redo", QKeySequence("Shift+U"), [this] { redo(); });
    edit->addSeparator();
    act(edit, "Move", QKeySequence("M"), [this] { startMove(); });
    act(edit, "Delete", QKeySequence::Delete, [this] { deleteSelection(); });
    act(edit, "Delete", QKeySequence(Qt::Key_Backspace), [this] { deleteSelection(); })->setVisible(false);
    act(edit, "Cancel", QKeySequence(Qt::Key_Escape), [this] { escape(); });

    QMenu* create = menuBar()->addMenu("&Create");
    if (isSchematic()) {
        act(edit, "Rotate", QKeySequence("R"), [this] { rotate(); });
        act(edit, "Properties…", QKeySequence("Q"), [this] { editProperties(); });
        act(create, "Instance…", QKeySequence("I"), [this] {
            QString l = masterLib_.isEmpty() ? lib_ : masterLib_, c = masterCell_, v = masterView_;
            if (pickMaster(this, session_, l, c, v)) startInstance(l, c, v);
        });
        act(create, "Wire", QKeySequence("W"), [this] { startWire(); });
        act(create, "Wire Name…", QKeySequence("L"), [this] {
            const QString names = QInputDialog::getText(this, "Wire Name", "Names (one per click):");
            if (!names.trimmed().isEmpty()) startLabel(names);
        });
        act(create, "Pin…", QKeySequence("P"), [this] {
            const QString names = QInputDialog::getText(this, "Pin", "Pin names (one per click):");
            if (names.trimmed().isEmpty()) return;
            bool ok = false;
            const QString dir = QInputDialog::getItem(this, "Pin", "Direction:", {"input", "output", "inputOutput"}, 0, false, &ok);
            if (ok) startPin(names, dir);
        });
        create->addSeparator();
        act(create, "Symbol from Schematic", {}, [this] {
            if (!generateSymbol()) QMessageBox::warning(this, "Symbol", status_->text());
        });
        QMenu* checkMenu = menuBar()->addMenu("Chec&k");
        act(checkMenu, "Check", QKeySequence("X"), [this] {
            const QJsonObject r = check();
            QStringList lines;
            for (const auto& e : r["errors"].toArray()) lines << "error: " + e.toString();
            for (const auto& w : r["warnings"].toArray()) lines << "warning: " + w.toString();
            if (!lines.isEmpty()) QMessageBox::information(this, "Check", lines.join("\n"));
        });
        QMenu* hier = menuBar()->addMenu("&Hierarchy");
        act(hier, "Descend", QKeySequence("Shift+E"), [this] { descend(); });
    } else {
        act(create, "Line", QKeySequence("L"), [this] { startShape(Tool::Line); });
        act(create, "Rectangle", QKeySequence("R"), [this] { startShape(Tool::Rect); });
        act(create, "Polygon", QKeySequence("G"), [this] { startShape(Tool::Polygon); });
        act(create, "Ellipse", QKeySequence("E"), [this] { startShape(Tool::Ellipse); });
        act(create, "Pin…", QKeySequence("P"), [this] {
            const QString names = QInputDialog::getText(this, "Pin", "Pin names (one per click):");
            if (names.trimmed().isEmpty()) return;
            bool ok = false;
            const QString dir = QInputDialog::getItem(this, "Pin", "Direction:", {"input", "output", "inputOutput"}, 0, false, &ok);
            if (ok) startPin(names, dir);
        });
        act(create, "Label…", QKeySequence("T"), [this] {
            const QString text = QInputDialog::getText(this, "Label", "Text ([@instanceName], [@cellName], [@param]):");
            if (!text.isEmpty()) startText(text);
        });
    }
    QMenu* viewMenu = menuBar()->addMenu("&View");
    act(viewMenu, "Fit", QKeySequence("F"), [this] { fit(); });
    act(viewMenu, "Zoom In", QKeySequence("]"), [this] { canvas_->zoomBy(2); });
    act(viewMenu, "Zoom Out", QKeySequence("["), [this] { canvas_->zoomBy(0.5); });
}

QPointF ViewEditor::snap(QPointF p) const {
    return {std::round(p.x() / kSnap) * kSnap, std::round(p.y() / kSnap) * kSnap};
}

double ViewEditor::tolerance() const { return 5 * canvas_->scale(); }

cmd::Result ViewEditor::call(const QString& name, QJsonObject args) {
    args["library"] = lib_;
    args["cell"] = cell_;
    args["view"] = view_;
    const cmd::Result r = session_.call(name, args);
    if (!r.ok) status_->setText("✗ " + r.error);
    return r;
}

void ViewEditor::reload() {
    const QString error = scene_.load(session_, lib_, cell_, view_);
    if (!error.isEmpty()) {
        status_->setText("This view cannot be read: " + error);
        selection_.clear();
        points_.clear();
        setTool(Tool::Select);
    }
    for (auto it = selection_.begin(); it != selection_.end();)  // what an undo or another editor removed
        it = scene_.find(it->first, it->second) < 0 ? selection_.erase(it) : std::next(it);
    redraw();
}

void ViewEditor::fit() { canvas_->fit(scene_.bounds()); }

void ViewEditor::setTool(Tool t) {
    tool_ = t;
    points_.clear();
    refSet_ = false;
    if (t == Tool::Select) names_.clear();
    status_->setText(prompt());
    redraw();
}

QString ViewEditor::prompt() const {
    switch (tool_) {
        case Tool::Select: return "Select: click, Shift-click to add, drag a box; drag a selected object to move it";
        case Tool::Move: return refSet_ ? "Move: point at where it goes" : "Move: point at the reference point";
        case Tool::Instance: return "Instance " + masterLib_ + "/" + masterCell_ + " (" + orient_ + "): point at where to place it (R rotates)";
        case Tool::Wire: return points_.empty() ? "Wire: point at the first point" : "Wire: next point (ends on a pin or wire; double-click or Esc ends)";
        case Tool::Label: return "Wire name " + names_.value(0) + ": point at the wire";
        case Tool::Pin: return "Pin " + names_.value(0) + " (" + direction_ + "): point at where to place it";
        case Tool::Line: return points_.empty() ? "Line: point at the first point" : "Line: next point (double-click or Esc ends)";
        case Tool::Polygon: return points_.empty() ? "Polygon: point at the first point" : "Polygon: next point (double-click closes)";
        case Tool::Rect: return points_.empty() ? "Rectangle: point at the first corner" : "Rectangle: point at the opposite corner";
        case Tool::Ellipse: return points_.empty() ? "Ellipse: point at a corner of its box" : "Ellipse: point at the opposite corner";
        case Tool::Text: return "Label \"" + names_.value(0) + "\": point at where to place it";
    }
    return {};
}

void ViewEditor::escape() {
    if (tool_ == Tool::Wire || tool_ == Tool::Line || tool_ == Tool::Polygon) finishPath();
    setTool(Tool::Select);
}

void ViewEditor::startInstance(const QString& lib, const QString& cell, const QString& view) {
    masterLib_ = lib, masterCell_ = cell, masterView_ = view;
    setTool(Tool::Instance);
}
void ViewEditor::startWire() { setTool(Tool::Wire); }
void ViewEditor::startLabel(const QString& names) {
    setTool(Tool::Select);
    names_ = names.split(' ', Qt::SkipEmptyParts);
    if (!names_.isEmpty()) setTool(Tool::Label);
}
void ViewEditor::startPin(const QString& names, const QString& direction) {
    setTool(Tool::Select);
    names_ = names.split(' ', Qt::SkipEmptyParts);
    direction_ = direction;
    if (!names_.isEmpty()) setTool(Tool::Pin);
}
void ViewEditor::startShape(Tool kind) { setTool(kind); }
void ViewEditor::startText(const QString& text) {
    setTool(Tool::Select);
    names_ = QStringList{text};
    setTool(Tool::Text);
}
void ViewEditor::startMove() {
    if (selection_.empty()) {
        status_->setText("Move: select what to move first");
        return;
    }
    setTool(Tool::Move);
}

void ViewEditor::rotate() {
    if (tool_ == Tool::Instance) {
        orient_ = nextTurn(orient_);
        status_->setText(prompt());
        redraw();
        return;
    }
    if (selection_.size() != 1 || selection_.begin()->first != "instance") {
        status_->setText("Rotate: select one instance");
        return;
    }
    const QString name = selection_.begin()->second;
    for (const auto& i : scene_.view()["instances"].toArray())
        if (i.toObject()["name"] == name) {
            const QJsonArray at = i.toObject()["at"].toArray();
            call("instance_move", {{"instance", name}, {"x", at[0]}, {"y", at[1]}, {"orient", nextTurn(i.toObject()["orient"].toString())}});
        }
}

// The next wire point, routed: across, then up or down
void ViewEditor::addRouted(QPointF p) {
    auto add = [this](QPointF q) {
        if (!points_.empty() && points_.back() == q) return;
        const size_t n = points_.size();
        if (n >= 2) {  // straight on: extend the last segment instead of adding a corner
            const QPointF a = points_[n - 2], b = points_[n - 1];
            if ((a.x() == b.x() && b.x() == q.x()) || (a.y() == b.y() && b.y() == q.y())) {
                points_.back() = q;
                return;
            }
        }
        points_.push_back(q);
    };
    if (points_.empty()) return add(p);
    const QPointF last = points_.back();
    if (last.x() != p.x() && last.y() != p.y()) add({p.x(), last.y()});
    add(p);
}

void ViewEditor::finishPath() {
    const std::vector<QPointF> pts = std::move(points_);
    points_.clear();
    QJsonArray a;
    for (const QPointF p : pts) a.append(xy(p));
    if (tool_ == Tool::Wire && pts.size() >= 2) call("wire_add", {{"points", a}});
    else if (tool_ == Tool::Line && pts.size() >= 2) call("shape_add", {{"kind", "line"}, {"points", a}});
    else if (tool_ == Tool::Polygon && pts.size() >= 3) call("shape_add", {{"kind", "polygon"}, {"points", a}});
    status_->setText(prompt());
}

void ViewEditor::press(QPointF world, Qt::KeyboardModifiers modifiers) {
    const QPointF p = snap(world);
    cursor_ = world;
    switch (tool_) {
        case Tool::Select: {
            pressing_ = true, dragged_ = false, pressAt_ = world;
            const int k = scene_.hit(world, tolerance());
            boxing_ = k < 0;
            if (k < 0) {
                if (!(modifiers & Qt::ShiftModifier)) selection_.clear();
            } else {
                const std::pair<QString, QString> key{scene_.items()[k].kind, scene_.items()[k].key};
                if (modifiers & Qt::ShiftModifier) {
                    if (!selection_.erase(key)) selection_.insert(key);
                } else if (!selection_.count(key)) {
                    selection_ = {key};
                }
            }
            break;
        }
        case Tool::Move:
            if (!refSet_) {
                ref_ = p, refSet_ = true;
                status_->setText(prompt());
            } else {
                QJsonArray objs;
                for (const auto& [kind, key] : selection_) objs.append(QJsonObject{{"kind", kind}, {"key", key}});
                const QPointF d = p - ref_;
                setTool(Tool::Select);
                if (!d.isNull()) call("objects_move", {{"objects", objs}, {"dx", d.x()}, {"dy", d.y()}});
            }
            break;
        case Tool::Instance:
            call("instance_add", {{"master_library", masterLib_}, {"master_cell", masterCell_}, {"master_view", masterView_},
                                  {"x", p.x()}, {"y", p.y()}, {"orient", orient_}});
            break;
        case Tool::Wire: {
            const bool first = points_.empty();
            addRouted(p);
            if (!first && points_.size() >= 2 && scene_.connects(p)) finishPath();  // ended on a pin or a wire
            status_->setText(prompt());
            break;
        }
        case Tool::Line:
        case Tool::Polygon:
            if (points_.empty() || points_.back() != p) points_.push_back(p);
            status_->setText(prompt());
            break;
        case Tool::Rect:
        case Tool::Ellipse:
            if (points_.empty()) {
                points_.push_back(p);
            } else if (p.x() != points_[0].x() && p.y() != points_[0].y()) {
                const QJsonArray a{xy(points_[0]), xy(p)};
                points_.clear();
                call("shape_add", {{"kind", tool_ == Tool::Rect ? "rect" : "ellipse"}, {"points", a}});
            }
            status_->setText(prompt());
            break;
        case Tool::Label:
        case Tool::Pin:
        case Tool::Text: {
            const QString name = names_.value(0);
            const cmd::Result r = tool_ == Tool::Label ? call("label_add", {{"name", name}, {"x", p.x()}, {"y", p.y()}})
                                  : tool_ == Tool::Pin ? call("pin_add", {{"name", name}, {"direction", direction_}, {"x", p.x()}, {"y", p.y()}})
                                                       : call("shape_add", {{"kind", "label"}, {"layer", "annotate"}, {"points", QJsonArray{xy(p)}}, {"text", name}});
            if (r.ok) names_.removeFirst();
            if (names_.isEmpty()) setTool(Tool::Select);
            else status_->setText(prompt());
            break;
        }
    }
    redraw();
}

void ViewEditor::moveTo(QPointF world) {
    cursor_ = world;
    if (pressing_ && QLineF(world, pressAt_).length() > 4 * canvas_->scale()) dragged_ = true;
    const QPointF s = snap(world);
    status_->setText(prompt() + QString("    x %1  y %2").arg(s.x()).arg(s.y()));
    redraw();
}

void ViewEditor::release(QPointF world) {
    if (tool_ != Tool::Select || !pressing_) return;
    pressing_ = false;
    if (dragged_ && boxing_) {
        for (int k : scene_.within(QRectF(pressAt_, world))) selection_.insert({scene_.items()[k].kind, scene_.items()[k].key});
    } else if (dragged_ && !selection_.empty()) {
        const QPointF d = snap(world) - snap(pressAt_);
        QJsonArray objs;
        for (const auto& [kind, key] : selection_) objs.append(QJsonObject{{"kind", kind}, {"key", key}});
        if (!d.isNull()) call("objects_move", {{"objects", objs}, {"dx", d.x()}, {"dy", d.y()}});
    }
    dragged_ = boxing_ = false;
    redraw();
}

void ViewEditor::doubleClick(QPointF world) {
    if (tool_ != Tool::Wire && tool_ != Tool::Line && tool_ != Tool::Polygon) return;
    press(world);  // the second click of the pair arrives as the double-click
    finishPath();
    redraw();
}

void ViewEditor::deleteSelection() {
    if (selection_.empty()) return;
    QJsonArray objs;
    for (const auto& [kind, key] : selection_) objs.append(QJsonObject{{"kind", kind}, {"key", key}});
    selection_.clear();
    call("objects_delete", {{"objects", objs}});
    redraw();
}

bool ViewEditor::undo() { return session_.call("edit_undo", {{"library", lib_}}).ok; }
bool ViewEditor::redo() { return session_.call("edit_redo", {{"library", lib_}}).ok; }

QJsonObject ViewEditor::check() {
    const cmd::Result r = call("schematic_check", {});
    const QJsonObject o = r.value.toObject();
    if (r.ok)
        status_->setText(QString("Check: %1 error(s), %2 warning(s)").arg(o["errors"].toArray().size()).arg(o["warnings"].toArray().size()));
    return o;
}

bool ViewEditor::generateSymbol() {
    const cmd::Result r = session_.call("symbol_generate", {{"library", lib_}, {"cell", cell_}, {"from", view_}});
    if (!r.ok) {
        status_->setText("✗ " + r.error);
        return false;
    }
    return session_.call("editor_open", {{"library", lib_}, {"cell", cell_}, {"view", r.value.toString()}}).ok;
}

bool ViewEditor::descend() {
    if (selection_.size() != 1 || selection_.begin()->first != "instance") {
        status_->setText("Descend: select one instance");
        return false;
    }
    for (const auto& i : scene_.view()["instances"].toArray()) {
        if (i.toObject()["name"] != selection_.begin()->second) continue;
        const QStringList m = i.toObject()["master"].toString().split('/');
        QString view = m.value(2);
        for (const auto& v : session_.call("view_list", {{"library", m.value(0)}, {"cell", m.value(1)}}).value.toArray())
            if (v.toObject()["type"] == "schematic") { view = v.toObject()["view"].toString(); break; }
        const cmd::Result r = session_.call("editor_open", {{"library", m.value(0)}, {"cell", m.value(1)}, {"view", view}});
        if (!r.ok) status_->setText("✗ " + r.error);
        return r.ok;
    }
    return false;
}

void ViewEditor::editProperties() {
    if (selection_.size() != 1 || selection_.begin()->first != "instance") {
        status_->setText("Properties: select one instance");
        return;
    }
    const QString name = selection_.begin()->second;
    QJsonObject inst;
    for (const auto& i : scene_.view()["instances"].toArray()) if (i.toObject()["name"] == name) inst = i.toObject();
    const QJsonObject params = inst["params"].toObject();
    QDialog d(this);
    d.setWindowTitle("Properties: " + name + " (" + inst["master"].toString() + ")");
    auto* form = new QFormLayout(&d);
    auto* table = new QTableWidget((int)params.size() + 1, 2, &d);  // a blank last row adds a parameter
    table->setHorizontalHeaderLabels({"Parameter", "Value"});
    table->horizontalHeader()->setStretchLastSection(true);
    int row = 0;
    for (auto it = params.begin(); it != params.end(); ++it, ++row) {
        table->setItem(row, 0, new QTableWidgetItem(it.key()));
        table->setItem(row, 1, new QTableWidgetItem(it.value().toString()));
    }
    form->addRow(table);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted) return;
    QJsonObject changed;  // every old name maps to "" unless kept: a cleared or renamed row removes it
    for (auto it = params.begin(); it != params.end(); ++it) changed[it.key()] = "";
    for (int r = 0; r < table->rowCount(); ++r) {
        const QString k = table->item(r, 0) ? table->item(r, 0)->text().trimmed() : QString();
        if (!k.isEmpty()) changed[k] = table->item(r, 1) ? table->item(r, 1)->text().trimmed() : QString();
    }
    call("instance_set_params", {{"instance", name}, {"params", changed}});
}

void ViewEditor::redraw() {
    Display d;
    std::set<int> sel;
    for (const auto& [kind, key] : selection_) if (const int k = scene_.find(kind, key); k >= 0) sel.insert(k);
    scene_.draw(d, sel);
    auto preview = [&](const Display& g, QPointF offset) {
        for (auto s : g.segs) d.segs.push_back({s.a + offset, s.b + offset, Role::Preview});
        for (auto t : g.texts) d.texts.push_back({t.at + offset, t.text, t.justify, Role::Preview});
    };
    const QPointF c = snap(cursor_);
    auto path = [&](std::vector<QPointF> pts) {
        for (size_t k = 1; k < pts.size(); ++k) d.segs.push_back({pts[k - 1], pts[k], Role::Preview});
    };
    switch (tool_) {
        case Tool::Select:
            if (pressing_ && dragged_ && boxing_) {
                const QPointF a = pressAt_, b = cursor_;
                path({a, {b.x(), a.y()}, b, {a.x(), b.y()}, a});
            } else if (pressing_ && dragged_) {
                for (int k : sel) preview(scene_.items()[k].geometry, c - snap(pressAt_));
            }
            break;
        case Tool::Move:
            if (refSet_) for (int k : sel) preview(scene_.items()[k].geometry, c - ref_);
            break;
        case Tool::Instance:
            preview(scene_.placed(session_, masterLib_, masterCell_, masterView_, c, orient_, "").geometry, {});
            break;
        case Tool::Wire:
            if (!points_.empty()) {
                std::vector<QPointF> pts = points_;
                if (pts.back().x() != c.x() && pts.back().y() != c.y()) pts.push_back({c.x(), pts.back().y()});
                pts.push_back(c);
                path(pts);
            }
            break;
        case Tool::Line:
        case Tool::Polygon:
            if (!points_.empty()) {
                std::vector<QPointF> pts = points_;
                pts.push_back(c);
                if (tool_ == Tool::Polygon && pts.size() > 2) pts.push_back(pts.front());
                path(pts);
            }
            break;
        case Tool::Rect:
        case Tool::Ellipse:
            if (!points_.empty()) {
                const QPointF a = points_[0];
                path({a, {c.x(), a.y()}, c, {a.x(), c.y()}, a});
            }
            break;
        case Tool::Label:
        case Tool::Pin:
        case Tool::Text:
            d.texts.push_back({c, names_.value(0), "lowerLeft", Role::Preview});
            break;
    }
    canvas_->setDisplay(std::move(d));
}

}  // namespace app
