// test_layout_edit: layout editing, offscreen. Every tool in the viewer ends in a command; commands refuse what they must;
// generators keep their rules by arithmetic (the PDK's own decks are tools/m3_gate.py's); connectivity tracks the wiring.
#include "app/layout_viewer.h"
#include "app/shell.h"
#include "layout/connectivity.h"
#include "layout/pcell.h"

#include <QApplication>
#include <QJsonArray>
#include <QSettings>
#include <QTreeWidget>

#include <cstdio>
#include <filesystem>
#include <string>
#include <unistd.h>

static int g_fail = 0;
static void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++g_fail;
}

using layout::Rect;

static Rect boxOf(const oadb::LayoutShape& s) {
    Rect r{INT64_MAX, INT64_MAX, INT64_MIN, INT64_MIN};
    for (const auto& p : s.points) {
        r.x0 = std::min(r.x0, p.x);
        r.y0 = std::min(r.y0, p.y);
        r.x1 = std::max(r.x1, p.x);
        r.y1 = std::max(r.y1, p.y);
    }
    return r;
}

int main(int argc, char** argv) {
    QApplication qapp(argc, argv);
    QApplication::setOrganizationName("OpenVLSI-test");
    QSettings().clear();
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("layout edit " + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);

    {   // Generators, by arithmetic on what they drew
        const layout::Tech& t = *layout::findTech("sg13g2");
        for (const auto& [w, l, ng] : {std::tuple{300, 130, 1}, {1000, 130, 1}, {4000, 200, 4}}) {
            const layout::Generated g = layout::mosCell(t, true, w, l, ng);
            int64_t activ = 0, gates = 0, cont = 0;
            bool onGrid = true;
            for (const auto& s : g.shapes) {
                const Rect r = boxOf(s);
                if (s.layer == 1) activ = r.y1 - r.y0;
                if (s.layer == 5 && r.x1 - r.x0 == l) ++gates;
                if (s.layer == 6) ++cont;
                for (const auto& p : s.points) onGrid = onGrid && p.x % t.grid == 0 && p.y % t.grid == 0;
            }
            check(onGrid && activ == w / ng && gates == ng && cont > 0 && g.terminals.count("D") && g.terminals.count("G") && g.terminals.count("S"),
                  "a transistor draws its fingers and declares its terminals: w=" + std::to_string(w) + " ng=" + std::to_string(ng));
        }
        const auto via = layout::viaShapes(t, 0, 1000, 1000);
        check(via.size() == 3 && via[0].layer == 19 && boxOf(via[0]).x1 - boxOf(via[0]).x0 == 190, "a via is a cut and its two metals");
    }

    {
        app::Theme::apply(qapp, app::Theme::Mode::Dark);
        app::Session session(QString::fromStdString((dir / "openvlsi.libs").string()));
        app::Shell shell(session);
        const auto call = [&](const QString& c, QJsonObject a = {}) { return session.call(c, a); };
        check(call("library_create", {{"library", "edit"}, {"path", QString::fromStdString((dir / "edit.oalib").string())}}).ok, "library_create");
        check(call("cell_create", {{"library", "edit"}, {"cell", "top"}}).ok, "cell_create");
        check(call("view_create", {{"library", "edit"}, {"cell", "top"}, {"view", "layout"}, {"type", "layout"}}).ok, "view_create");
        const QJsonObject where{{"library", "edit"}, {"cell", "top"}, {"view", "layout"}};
        const auto with = [&](QJsonObject a) {
            for (auto it = where.begin(); it != where.end(); ++it) a[it.key()] = it.value();
            return a;
        };
        const auto rect = QJsonArray{QJsonArray{0, 0}, QJsonArray{100, 100}};

        // Refusals
        check(!call("layout_shape_add", with({{"layer", "Metal1"}, {"kind", "rect"}, {"points", rect}})).ok, "a layer name needs a process");
        check(call("layout_shape_add", with({{"layer", "8/0"}, {"kind", "rect"}, {"points", rect}})).ok, "numbers do not");
        check(!call("layout_tech_set", {{"library", "edit"}, {"tech", "nope"}}).ok, "an unknown process is refused");
        check(call("layout_tech_set", {{"library", "edit"}, {"tech", "sg13g2"}}).ok, "layout_tech_set");
        check(!call("layout_shape_add", with({{"layer", "Nope"}, {"kind", "rect"}, {"points", rect}})).ok, "an unknown layer is refused");
        check(!call("layout_shape_add", with({{"layer", "Metal1"}, {"kind", "blob"}, {"points", rect}})).ok, "an unknown kind is refused");
        check(!call("layout_shape_add", with({{"layer", "Metal1"}, {"kind", "rect"}, {"points", QJsonArray{QJsonArray{0, 0}, QJsonArray{0, 100}}}})).ok, "a rect with no area is refused");
        check(!call("layout_shape_add", with({{"layer", "Metal1"}, {"kind", "path"}, {"points", rect}})).ok, "a path with no width is refused");
        check(!call("layout_objects_delete", with({{"shapes", QJsonArray{99999}}})).ok, "deleting a missing object is refused");
        check(!call("layout_objects_move", with({{"dx", 1}, {"dy", 1}})).ok, "moving nothing is refused");
        check(!call("layout_instance_add", with({{"master", "top"}, {"at", QJsonArray{0, 0}}})).ok, "a cell placing itself is refused");
        check(!call("layout_instance_add", with({{"master", "absent"}, {"at", QJsonArray{0, 0}}})).ok, "a missing master is refused");
        check(call("layout_objects_get", where).value.toObject()["instances"].toArray().isEmpty(), "and a refused placement leaves nothing");
        check(!call("layout_generate", {{"library", "edit"}, {"cell", "top"}}).ok, "generating over a layout view is refused");
        check(!call("layout_connectivity", where).ok, "connectivity needs a generated layout");

        // The viewer's tools
        check(call("editor_open", where).ok, "editor_open");
        app::LayoutViewer* v = shell.ciw()->layout("edit", "top", "layout");
        check(v != nullptr, "the viewer opens");
        const auto shapes = [&] { return call("layout_objects_get", where).value.toObject()["shapes"].toArray(); };
        const qsizetype start = shapes().size();
        check(v->layerTree()->topLevelItemCount() > 20, "the panel offers the process's layers, used or not");

        v->setCurrentLayer(10, 0);  // Metal2
        v->startTool(app::LayoutViewer::Tool::Rect);
        v->clickDbu(1000, 1000);
        v->clickDbu(1502, 1203);    // snaps to the 5 nm grid
        check(shapes().size() == start + 1, "Rectangle draws on two clicks");
        QJsonObject added = shapes().last().toObject();
        check(added["layer"].toInt() == 10 && added["points"].toArray()[2].toArray()[0].toInt() == 1500 && added["points"].toArray()[2].toArray()[1].toInt() == 1205, "on the current layer, on the grid");

        v->startTool(app::LayoutViewer::Tool::Path);
        v->clickDbu(2000, 2000);
        v->clickDbu(3000, 2000);
        v->clickDbu(3000, 2500);
        v->finishTool();
        added = shapes().last().toObject();
        check(added["kind"] == "path" && added["points"].toArray().size() == 3 && added["width"].toInt() == 200, "Path ends on Enter, at the metal's minimum width");

        v->startTool(app::LayoutViewer::Tool::Polygon);
        v->clickDbu(4000, 0);
        v->clickDbu(4500, 0);
        v->finishTool();
        check(shapes().last().toObject()["kind"] == "path", "a polygon of two points does not draw");
        v->startTool(app::LayoutViewer::Tool::Polygon);
        v->clickDbu(4000, 0);
        v->clickDbu(4500, 0);
        v->clickDbu(4500, 500);
        v->finishTool();
        check(shapes().last().toObject()["kind"] == "polygon", "three do");

        const qsizetype beforeVia = shapes().size();
        v->setCurrentLayer(8, 0);
        v->startTool(app::LayoutViewer::Tool::Via);
        v->clickDbu(6000, 6000);
        check(shapes().size() == beforeVia + 3, "Via draws its cut and two metals");
        v->setCurrentLayer(19, 0);
        v->startTool(app::LayoutViewer::Tool::Via);
        check(v->tool() != app::LayoutViewer::Tool::Via, "a via needs a metal as its lower layer");

        v->setCurrentLayer(8, 0);
        v->setToolText("vdd");
        v->startTool(app::LayoutViewer::Tool::Pin);
        v->clickDbu(7000, 7000);
        v->clickDbu(7300, 7300);
        check(shapes().last().toObject()["text"] == "vdd" && shapes().last().toObject()["datatype"].toInt() == 25, "Pin draws its metal, pin shape and label");
        v->cancelTool();

        // Selection and what acts on it
        v->setCurrentLayer(10, 0);
        QJsonObject hit = v->selectAtDbu(1200, 1100);
        check(hit["layer"].toInt() == 10 && v->selection().size() == 1, "a click selects the shape");
        const double rectId = hit["object"].toDouble();
        v->startTool(app::LayoutViewer::Tool::Move);
        v->clickDbu(1000, 1000);
        v->clickDbu(1100, 1200);
        const auto find = [&](double id) {
            for (const auto& s : shapes()) if (s.toObject()["id"].toDouble() == id) return s.toObject();
            return QJsonObject{};
        };
        check(find(rectId)["points"].toArray()[0].toArray()[0].toInt() == 1100 && find(rectId)["points"].toArray()[0].toArray()[1].toInt() == 1200, "Move moves it by the two clicks");
        check(v->tool() == app::LayoutViewer::Tool::Select && v->selection().count((int64_t)rectId), "and keeps it selected, back in Select");
        check(v->canvas()->selectedBoxes.size() == 1, "the selection is drawn from the reloaded geometry");

        const qsizetype beforeCopy = shapes().size();
        v->startTool(app::LayoutViewer::Tool::Copy);
        v->clickDbu(0, 0);
        v->clickDbu(0, 2000);
        check(shapes().size() == beforeCopy + 1, "Copy adds one");

        // Stretch the top edge up by 100: only that edge's vertices move
        v->selectAtDbu(1200, 1200);
        check(!v->selection().empty(), "reselected");
        v->startTool(app::LayoutViewer::Tool::Stretch);
        v->clickDbu(1300, 1405);   // its top edge, y = 1405
        v->clickDbu(1900, 1505);   // sideways motion is ignored: a level edge moves up and down
        const QJsonArray stretched = find(rectId)["points"].toArray();
        check(stretched[0].toArray()[1].toInt() == 1200 && stretched[2].toArray()[1].toInt() == 1505 && stretched[2].toArray()[0].toInt() == 1600, "Stretch moves one edge, along its normal");
        check(!call("layout_stretch", with({{"shapes", QJsonArray{rectId}}, {"window", QJsonArray{-9, -9, -8, -8}}, {"dx", 1}, {"dy", 1}})).ok, "a window with no vertex is refused");

        const qsizetype beforeDelete = shapes().size();
        v->deleteSelection();
        check(shapes().size() == beforeDelete - 1 && v->selection().empty(), "Delete removes the selection");
        v->undo();
        check(shapes().size() == beforeDelete && find(rectId).size() > 0, "Undo brings it back");
        v->redo();
        check(shapes().size() == beforeDelete - 1, "Redo removes it again");
        v->undo();

        // Instances: selected as one, moved as one
        QJsonObject pc{{"library", "edit"}, {"type", "nmos"}, {"w", 1000}, {"l", 130}};
        const QString master = call("layout_pcell_mos", pc).value.toString();
        check(master == "nmos_w1000_l130_ng1", "layout_pcell_mos names a master by its size: " + master.toStdString());
        check(call("layout_pcell_mos", pc).value.toString() == master, "and generates it once");
        v->setInstanceMaster(master);
        v->startTool(app::LayoutViewer::Tool::Instance);
        v->clickDbu(20000, 0);
        v->cancelTool();
        const QJsonArray insts = call("layout_objects_get", where).value.toObject()["instances"].toArray();
        check(insts.size() == 1 && insts[0].toObject()["cell"] == master, "Instance places the master on a click");
        v->clickDbu(20150, 300);    // a source contact's area
        check(v->selection() == std::set<int64_t>{-(int64_t)insts[0].toObject()["id"].toDouble()} && v->canvas()->selectedBoxes.size() == 1, "a click on what it draws selects the instance");
        v->startTool(app::LayoutViewer::Tool::Move);
        v->clickDbu(0, 0);
        v->clickDbu(500, 0);
        check(call("layout_objects_get", where).value.toObject()["instances"].toArray()[0].toObject()["at"].toArray()[0].toInt() == 20500, "Move moves it");
        check(v->status().size() > 0, "the status line says something");

    }
    QSettings().clear();
    fs::remove_all(dir);
    std::printf(g_fail ? "FAILED (%d)\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
