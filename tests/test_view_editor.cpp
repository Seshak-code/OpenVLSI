// test_view_editor: the schematic and symbol editors, offscreen, driven through their tools in database units. Each
// tool's clicks become the right command (snapped to the grid); a wire routes across-then-up and ends itself on a pin;
// selection picks, adds, boxes, drags and deletes as single undo steps; the editor follows the database whoever
// changes it; editor_open, descend and symbol generation open the right windows; the canvas draws (when the platform
// gives it a GPU).
#include "app/canvas.h"
#include "app/ciw.h"
#include "app/view_editor.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unistd.h>

static int g_fail = 0;
static void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++g_fail;
}
static std::string str(const QJsonValue& v) { return QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact).toStdString(); }
static QJsonArray xy(double x, double y) { return QJsonArray{x, y}; }

int main(int argc, char** argv) {
    QApplication qapp(argc, argv);
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("editor test " + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    const QString ws = QString::fromStdString((dir / "openvlsi.libs").string());
    using Tool = app::ViewEditor::Tool;
    {
        app::Session session(ws);
        app::Ciw ciw(session);
        auto must = [&](const char* name, const QJsonObject& a) {
            const cmd::Result r = session.call(name, a);
            if (!r.ok) check(false, std::string(name) + " must succeed: " + r.error.toStdString());
            return r.value;
        };
        auto view = [&](const char* cell, const char* v) { return must("view_get", {{"library", "analog"}, {"cell", cell}, {"view", v}}).toObject(); };
        auto exported = [&] { return must("library_export", {{"library", "analog"}}).toString(); };
        must("library_create", {{"library", "analog"}, {"path", QString::fromStdString((dir / "analog.oalib").string())}});
        for (const char* c : {"buf", "top"}) must("cell_create", {{"library", "analog"}, {"cell", c}});
        must("view_create", {{"library", "analog"}, {"cell", "buf"}, {"view", "symbol"}, {"type", "symbol"}});
        must("view_create", {{"library", "analog"}, {"cell", "top"}, {"view", "schematic"}, {"type", "schematic"}});

        // ── the symbol editor ──
        check(must("editor_open", {{"library", "analog"}, {"cell", "buf"}, {"view", "symbol"}}).toString() == "symbol",
              "editor_open opens a symbol view");
        app::ViewEditor* sym = ciw.editor("analog", "buf", "symbol");
        check(sym && !sym->isSchematic() && sym->windowTitle() == "Symbol Editing: analog buf symbol", "the symbol editor's window");
        sym->startShape(Tool::Rect);
        sym->press({0, 0});
        sym->press({0, 100});  // no area: not a rectangle yet
        sym->press({200, 210});
        sym->startShape(Tool::Line);
        sym->press({-100, 100});
        sym->doubleClick({0, 100});
        sym->startPin("A", "input");
        sym->press({-103, 98});
        sym->startText("[@instanceName]");
        sym->press({0, 250});
        auto s = view("buf", "symbol");
        const auto shapes = s["shapes"].toArray();
        check(shapes.size() == 3 && shapes[0].toObject()["kind"] == "rect" &&
                  shapes[0].toObject()["points"].toArray() == QJsonArray{xy(0, 0), xy(200, 200)} &&
                  shapes[1].toObject()["points"].toArray() == QJsonArray{xy(-100, 100), xy(0, 100)} &&
                  shapes[2].toObject()["text"] == "[@instanceName]" && shapes[2].toObject()["justify"] == "centerLeft",
              "rectangle (a zero-area corner ignored), line (double-click ends) and label, snapped to the grid (" + str(shapes) + ")");
        check(s["pins"].toArray().size() == 1 && s["pins"].toArray()[0].toObject()["at"].toArray() == xy(-100, 100) &&
                  sym->tool() == Tool::Select,
              "the pin snaps to (-100, 100); placing the last name ends the tool");
        check(sym->undo() && view("buf", "symbol")["shapes"].toArray().size() == 2 && sym->scene().items().size() == 3 && sym->redo() &&
                  sym->scene().items().size() == 4,
              "undo and redo, and the editor follows each");

        // ── the schematic editor ──
        must("editor_open", {{"library", "analog"}, {"cell", "top"}, {"view", "schematic"}});
        app::ViewEditor* top = ciw.editor("analog", "top", "schematic");
        QCoreApplication::processEvents();
        top->canvas()->fit(QRectF(-500, -500, 3000, 3000));
        check(top && top->isSchematic() && top->canvas()->scale() > 1 && top->canvas()->scale() < 20,
              "the schematic editor is open, its canvas laid out");

        top->startInstance("analog", "buf");
        top->press({1000, 0});
        top->press({1000, 600});
        top->rotate();
        top->press({1610, -5});
        top->escape();
        QStringList placed;
        for (const auto& i : view("top", "schematic")["instances"].toArray())
            placed << i.toObject()["name"].toString() + "@" + QString::fromStdString(str(i.toObject()["at"])) + i.toObject()["orient"].toString();
        check(placed == QStringList{"I0@[[1000,0]]R0", "I1@[[1000,600]]R0", "I2@[[1600,0]]R90"},
              "Instance places, names and rotates (R), snapped (" + placed.join(" ").toStdString() + ")");

        top->startWire();
        top->press({600, 300});
        top->press({900, 100});  // I0's pin A: (1000, 0) + (-100, 100)
        auto wires = view("top", "schematic")["wires"].toArray();
        check(wires.size() == 1 && wires[0].toObject()["points"].toArray() == QJsonArray{xy(600, 300), xy(900, 300), xy(900, 100)} &&
                  top->tool() == Tool::Wire && top->prompt().contains("first point"),
              "a wire routes across then up, and ends itself on a pin (" + str(wires) + ")");
        top->press({600, 300});
        top->press({600, 400});
        top->press({600, 500});  // straight on: one segment, not two
        top->escape();
        wires = view("top", "schematic")["wires"].toArray();
        check(wires.size() == 2 && wires[1].toObject()["points"].toArray() == QJsonArray{xy(600, 300), xy(600, 500)},
              "Esc keeps a wire in progress; a straight run stays one segment");

        top->startLabel("in");
        top->press({600, 300});
        bool in = false;
        for (const auto& n : must("schematic_check", {{"library", "analog"}, {"cell", "top"}, {"view", "schematic"}}).toObject()["nets"].toArray())
            in = in || (n.toObject()["name"] == "in" && str(n.toObject()["terminals"]).find("\"I0\"") != std::string::npos);
        check(in && top->tool() == Tool::Select, "a wire name labels the net that reaches I0's pin");
        QStringList texts;
        for (const auto& t : top->canvas()->display().texts) texts << t.text;
        check(texts.contains("I0") && texts.contains("I2") && texts.contains("in"), "the canvas shows instance names and the label");

        const QString wire0 = QString::number((qint64)wires[0].toObject()["id"].toDouble());
        top->press({750, 300});
        top->release({750, 300});
        check(top->selection() == std::set<std::pair<QString, QString>>{{"wire", wire0}}, "a click selects the wire under it");
        top->press({610, 320}, Qt::ShiftModifier);  // the label's text, above its point
        top->release({610, 320});
        check(top->selection().size() == 2 && top->selection().count({"label", "1"}), "Shift-click adds the label");
        const QString beforeMove = exported();
        top->press({750, 300});
        top->moveTo({750, 200});
        top->release({752, 198});
        const auto moved = view("top", "schematic");
        check(moved["wires"].toArray()[0].toObject()["points"].toArray() == QJsonArray{xy(600, 200), xy(900, 200), xy(900, 0)} &&
                  moved["labels"].toArray()[0].toObject()["at"].toArray() == xy(600, 200),
              "dragging the selection moves both, snapped");
        check(top->undo() && exported() == beforeMove, "one undo puts both back");

        top->press({-2000, -2000});
        top->moveTo({5000, 5000});
        top->release({5000, 5000});
        check(top->selection().size() == top->scene().items().size() && top->selection().size() == 6,
              "a box selects all it encloses (" + std::to_string(top->selection().size()) + " of " + std::to_string(top->scene().items().size()) + ")");
        top->deleteSelection();
        const auto empty = view("top", "schematic");
        check(empty["instances"].toArray().isEmpty() && empty["wires"].toArray().isEmpty() && top->scene().items().empty(),
              "Delete removes the selection");
        check(top->undo() && exported() == beforeMove && top->selection().empty(), "one undo brings it all back");

        top->press({1100, 100});  // inside I0's box
        top->release({1100, 100});
        top->rotate();
        top->startMove();
        top->press({1000, 0});
        top->press({1100, -100});
        QJsonObject i0;
        for (const auto& i : view("top", "schematic")["instances"].toArray()) if (i.toObject()["name"] == "I0") i0 = i.toObject();
        check(i0["orient"] == "R90" && i0["at"].toArray() == xy(1100, -100), "rotate the selected instance; Move: reference, then target");

        check(top->descend() && ciw.editor("analog", "buf", "symbol") == sym, "descend opens the master (the symbol: buf has no schematic)");
        top->startPin("vin vout", "input");
        top->press({0, 0});
        check(top->tool() == Tool::Pin && top->prompt().contains("vout"), "pins: one name per click");
        top->press({0, -200});
        check(view("top", "schematic")["pins"].toArray().size() == 2 && top->generateSymbol() && ciw.editor("analog", "top", "symbol"),
              "Symbol from Schematic generates the symbol and opens it");
        check(top->check().contains("warnings") && !top->generateSymbol(), "check returns the report; a second generate fails");

        const size_t n = top->scene().items().size();
        {
            auto other = oadb::Workspace::load(ws.toStdString());  // another process
            other.library("analog").addWire("top", "schematic", {0, {{-500, -500}, {-400, -500}}});
        }
        top->reload();
        check(top->scene().items().size() == n + 1, "reload shows another writer's wire");
        ciw.runLine(R"(wire_add {"library": "analog", "cell": "top", "view": "schematic", "points": [[-500, -600], [-400, -600]]})");
        check(top->scene().items().size() == n + 2, "a change typed in the CIW redraws the editor");

        must("view_create", {{"library", "analog"}, {"cell", "top"}, {"view", "layout"}, {"type", "layout"}});
        check(!session.call("editor_open", {{"library", "analog"}, {"cell", "top"}, {"view", "layout"}}).ok &&
                  !session.call("editor_open", {{"library", "analog"}, {"cell", "top"}, {"view", "nope"}}).ok,
              "editor_open refuses a layout and a missing view");

        app::Scene scene;
        scene.load(session, "analog", "top", "schematic");
        const app::Item mirrored = scene.placed(session, "analog", "buf", "symbol", {0, 0}, "MY", "X");
        QString justify;
        for (const auto& t : mirrored.geometry.texts) if (t.text == "X") justify = t.justify;
        check(justify == "centerRight", "a mirrored instance's left-justified label is right-justified (" + justify.toStdString() + ")");

        const QImage frame = top->canvas()->grabFramebuffer();
        if (const char* to = std::getenv("OPENVLSI_GRAB")) top->grab().save(to);  // the window, texts included, to look at
        if (frame.isNull()) {
            std::printf("  [SKIP] the canvas has no GPU on this platform: nothing to grab\n");
        } else {
            int wire = 0;
            for (int y = 0; y < frame.height(); ++y)
                for (int x = 0; x < frame.width(); ++x) {
                    const QColor c = frame.pixelColor(x, y);
                    wire += c.blue() > 200 && c.red() < 120;
                }
            check(wire > 50, "the canvas draws the wires (" + std::to_string(wire) + " wire-coloured pixels)");
        }
    }
    fs::remove_all(dir);
    std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "OK", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
