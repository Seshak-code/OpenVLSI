// test_layout: layout data and its viewer, offscreen. A GDS written here reads back the same; references and arrays land
// where their transforms say, by hand; polygons tessellate to their area; a bad file is refused and leaves the library
// alone; export round-trips; the viewer's selection, layers and levels work without a GPU.
#include "app/layout_viewer.h"
#include "app/shell.h"
#include "layout/flatten.h"
#include "layout/gds.h"

#include <QApplication>
#include <QJsonArray>
#include <QSettings>
#include <QTreeWidget>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

static int g_fail = 0;
static void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++g_fail;
}
static bool near(double a, double b, double tol = 1e-9) { return std::abs(a - b) <= tol; }

using oadb::Point;

static oadb::LayoutShape rect(int layer, int64_t x0, int64_t y0, int64_t x1, int64_t y1) {
    oadb::LayoutShape s;
    s.layer = layer;
    s.kind = "polygon";
    s.points = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}, {x0, y0}};
    return s;
}

static double triArea(const std::vector<double>& xy) {
    double a = 0;
    for (size_t k = 0; k + 6 <= xy.size(); k += 6)
        a += std::abs((xy[k + 2] - xy[k]) * (xy[k + 5] - xy[k + 1]) - (xy[k + 4] - xy[k]) * (xy[k + 3] - xy[k + 1])) / 2;
    return a;
}

int main(int argc, char** argv) {
    QApplication qapp(argc, argv);
    QApplication::setOrganizationName("OpenVLSI-test");
    QSettings().clear();
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("layout test " + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string file = (dir / "t.gds").string();

    {   // Tessellation: an L, a keyhole frame and a collinear vertex, by area
        std::vector<double> xy;
        check(layout::triangulate({{0, 0}, {30, 0}, {30, 10}, {10, 10}, {10, 30}, {0, 30}}, xy) && near(triArea(xy), 500), "an L tessellates to its area");
        xy.clear();
        // a 10 x 10 square with a 4 x 4 hole, joined by a seam at x = 3 .. 0: 100 - 16
        check(layout::triangulate({{0, 0}, {10, 0}, {10, 10}, {0, 10}, {0, 3}, {3, 3}, {3, 7}, {7, 7}, {7, 3}, {3, 3}, {0, 3}}, xy) && near(triArea(xy), 84), "a keyhole polygon's hole stays open");
        xy.clear();
        check(layout::triangulate({{0, 0}, {5, 0}, {10, 0}, {10, 10}, {0, 10}}, xy) && near(triArea(xy), 100), "a collinear vertex adds nothing");
        const auto q = layout::pathQuads({{0, 0}, {100, 0}}, 10, 2);
        check(q.size() == 8 && near(q[0], -5) && near(q[1], 5) && near(q[4], 105) && near(q[5], -5), "an extended path grows half its width at each end");
        const auto f = layout::pathQuads({{0, 0}, {100, 0}}, 10, 0);
        check(near(f[0], 0) && near(f[4], 100), "a flush path stops at its ends");
    }

    {   // A GDS written and read: the same objects; real8 and arrays survive
        layout::GdsLibrary lib;
        lib.metersPerDbu = 1e-9;
        lib.userPerDbu = 1e-3;
        layout::GdsCell leaf;
        leaf.name = "leaf";
        leaf.shapes.push_back(rect(8, 0, 0, 100, 50));
        oadb::LayoutShape path;
        path.layer = 10;
        path.kind = "path";
        path.points = {{0, 200}, {100, 200}};
        path.width = 20;
        path.pathType = 2;
        leaf.shapes.push_back(path);
        oadb::LayoutShape text;
        text.layer = 8;
        text.datatype = 25;
        text.kind = "text";
        text.points = {{10, 10}};
        text.text = "VDD";
        leaf.shapes.push_back(text);
        layout::GdsCell top;
        top.name = "top";
        oadb::LayoutInstance plain;     // at (1000, 0)
        plain.cell = "leaf";
        plain.at = {1000, 0};
        oadb::LayoutInstance rotated;   // 90 degrees: (x, y) -> (-y, x)
        rotated.cell = "leaf";
        rotated.at = {0, 1000};
        rotated.angle = 90;
        oadb::LayoutInstance mirrored;  // about x, then at (2000, 0): (x, y) -> (x, -y)
        mirrored.cell = "leaf";
        mirrored.at = {2000, 0};
        mirrored.mirrorX = true;
        oadb::LayoutInstance array;     // 3 x 2 at pitches 500 and 600
        array.cell = "leaf";
        array.at = {5000, 5000};
        array.cols = 3;
        array.rows = 2;
        array.colStep = {500, 0};
        array.rowStep = {0, 600};
        top.instances = {plain, rotated, mirrored, array};
        lib.cells = {leaf, top};
        layout::writeGds(file, lib);

        const layout::GdsLibrary back = layout::readGds(file);
        check(back.cells.size() == 2 && back.cells[0].shapes.size() == 3 && back.cells[1].instances.size() == 4, "a written GDS reads back its objects");
        check(near(back.metersPerDbu, 1e-9, 1e-20) && near(back.userPerDbu, 1e-3, 1e-14), "its units survive (real8)");
        const auto& rb = back.cells[1].instances;
        check(near(rb[1].angle, 90) && rb[2].mirrorX && rb[3].cols == 3 && rb[3].rows == 2 && rb[3].colStep == Point{500, 0} && rb[3].rowStep == Point{0, 600}, "references keep their transforms");
        check(back.cells[0].shapes[1].width == 20 && back.cells[0].shapes[1].pathType == 2 && back.cells[0].shapes[2].text == "VDD", "paths and texts keep theirs");
    }

    {   // Refusals: a truncated file, not a GDS, an undefined reference
        const std::string data = [&] { std::ifstream f(file, std::ios::binary); return std::string((std::istreambuf_iterator<char>(f)), {}); }();
        const std::string cut = (dir / "cut.gds").string(), junk = (dir / "junk.gds").string();
        std::ofstream(cut, std::ios::binary).write(data.data(), (std::streamsize)data.size() / 2);
        std::ofstream(junk, std::ios::binary) << "this is not a stream file at all";
        auto refused = [](const std::string& p) { try { layout::readGds(p); return std::string(); } catch (const std::exception& e) { return std::string(e.what()); } };
        check(!refused(cut).empty(), "a truncated file is refused (" + refused(cut) + ")");
        check(!refused(junk).empty(), "a file that is not GDS is refused (" + refused(junk) + ")");
    }

    {
        app::Theme::apply(qapp, app::Theme::Mode::Dark);
        app::Session session(QString::fromStdString((dir / "openvlsi.libs").string()));
        app::Shell shell(session);
        const auto call = [&](const QString& c, QJsonObject a = {}) { return session.call(c, a); };
        check(call("library_create", {{"library", "chip"}, {"path", QString::fromStdString((dir / "chip.oalib").string())}}).ok, "library_create");

        const fs::path lyp = dir / "t.lyp";
        std::ofstream(lyp) << "<layer-properties><properties><frame-color>#39bfff</frame-color><visible>true</visible><name>Metal1</name><source>8/0</source></properties>"
                              "<properties><frame-color>#ff8000</frame-color><visible>false</visible><name>Metal2</name><source>Metal2 10/0@1</source></properties></layer-properties>";
        const cmd::Result imported = call("layout_import_gds", {{"library", "chip"}, {"file", QString::fromStdString(file)}, {"layers", QString::fromStdString(lyp.string())}});
        check(imported.ok, "layout_import_gds " + imported.error.toStdString());
        const QJsonObject o = imported.value.toObject();
        check(o["cells"].toInt() == 2 && o["tops"].toArray().first().toString() == "top" && o["shapes"].toInt() == 3 && o["instances"].toInt() == 4, "it reports the cells, top and counts");
        const QJsonArray layers = call("layout_layers", {{"library", "chip"}}).value.toArray();
        check(layers.size() == 2 && layers[1].toObject()["name"] == "Metal2" && layers[1].toObject()["datatype"].toInt() == 0 && !layers[1].toObject()["visible"].toBool(), "the .lyp names the layers");
        check(!call("layout_import_gds", {{"library", "chip"}, {"file", QString::fromStdString(file)}}).ok, "importing over an existing layout is refused");
        const size_t cellsBefore = call("cell_list", {{"library", "chip"}}).value.toArray().size();
        check(!call("layout_import_gds", {{"library", "chip"}, {"file", QString::fromStdString((dir / "cut.gds").string())}}).ok && call("cell_list", {{"library", "chip"}}).value.toArray().size() == (qsizetype)cellsBefore, "a refused import changes nothing");

        // The hierarchy, by hand. leaf's polygon spans x 0..100, y 0..50; its path spans x -10..110, y 190..210
        const cmd::Result info = call("layout_info", {{"library", "chip"}, {"cell", "top"}});
        check(info.ok, "layout_info " + info.error.toStdString());
        const QJsonObject in = info.value.toObject();
        check(in["instances"].toInt() == 9 && in["shapes"].toInt() == 18 && in["labels"].toInt() == 9, "placements are counted through arrays: 3 + 6 instances, 2 shapes each");
        // x from the rotated instance's path: -210 .. and the array's last column: 5000 + 2*500 + 110 = 6110. y from the mirrored one's
        // path (-210) and the array's top: 5000 + 600 + 210 = 5810. In microns at 1 nm a unit.
        const QJsonObject box = in["box"].toObject();
        check(near(box["x0"].toDouble(), -0.21) && near(box["x1"].toDouble(), 6.11) && near(box["y0"].toDouble(), -0.21) && near(box["y1"].toDouble(), 5.81), "the box follows rotation, mirroring and arrays");

        {   // The polygons themselves land by hand too
            layout::Hierarchy h(session.workspace(), "chip", "top", "layout");
            const layout::Flat flat = h.flatten();
            auto hasBox = [&](double x0, double y0, double x1, double y1) {
                for (const auto& s : flat.shapes)
                    if (near(s.x0 + flat.originX, x0, 1e-3) && near(s.y0 + flat.originY, y0, 1e-3) && near(s.x1 + flat.originX, x1, 1e-3) && near(s.y1 + flat.originY, y1, 1e-3)) return true;
                return false;
            };
            check(hasBox(1000, 0, 1100, 50), "a plain instance translates");
            check(hasBox(-50, 1000, 0, 1100), "90 degrees sends (x, y) to (-y, x)");
            check(hasBox(2000, -50, 2100, 0), "mirroring flips y");
            check(hasBox(6000, 5600, 6100, 5650), "an array's last element is at its pitches");
            const layout::Flat top = h.flatten(0);
            check(top.shapes.empty() && top.boxes.size() == 9 * 4, "level 0 draws masters as boxes");
        }

        // Export, then import it elsewhere: the same text
        const std::string out = (dir / "out.gds").string();
        check(call("layout_export_gds", {{"library", "chip"}, {"cell", "top"}, {"file", QString::fromStdString(out)}}).value.toInt() == 2, "layout_export_gds writes both structures");
        check(call("library_create", {{"library", "again"}, {"path", QString::fromStdString((dir / "again.oalib").string())}}).ok, "a second library");
        check(call("layout_import_gds", {{"library", "again"}, {"file", QString::fromStdString(out)}}).ok, "its import");
        std::string a = session.workspace().library("chip").exportText(), b = session.workspace().library("again").exportText();
        a = a.substr(a.find('\n'));
        b = b.substr(b.find('\n'));
        check(a == b, "the round trip is exact");

        // The viewer, in a tab
        check(call("editor_open", {{"library", "chip"}, {"cell", "top"}, {"view", "layout"}}).ok, "editor_open on a layout");
        check(shell.tabTitles().contains("top layout") && !shell.ciw()->layout("chip", "top", "layout")->isWindow(), "it opens as a tab");
        app::LayoutViewer* v = shell.ciw()->layout("chip", "top", "layout");
        check(v->layerTree()->topLevelItemCount() == 2, "the panel lists the layers with shapes");
        check(v->layerTree()->topLevelItem(1)->checkState(0) == Qt::Unchecked && v->layerTree()->topLevelItem(1)->text(0).startsWith("Metal2"), "and the .lyp's visibility");
        const cmd::Result hit = call("layout_view_select", {{"library", "chip"}, {"cell", "top"}, {"view", "layout"}, {"x", 1050}, {"y", 25}});
        check(hit.ok && hit.value.toObject()["layer"].toInt() == 8 && hit.value.toObject()["master"] == "leaf" && near(hit.value.toObject()["width_um"].toDouble(), 0.1, 1e-6), "selecting finds the shape, its master and size");
        check(!call("layout_view_select", {{"library", "chip"}, {"cell", "top"}, {"view", "layout"}, {"x", 3000}, {"y", 3000}}).ok, "nothing there is an error");
        check(call("layout_view_layer", {{"library", "chip"}, {"cell", "top"}, {"view", "layout"}, {"layer", 8}, {"datatype", 0}, {"visible", false}}).ok, "layout_view_layer hides a layer");
        check(call("layout_view_layer", {{"library", "chip"}, {"cell", "top"}, {"view", "layout"}, {"layer", 8}, {"datatype", 25}, {"visible", false}}).ok, "and its label's");
        check(!call("layout_view_select", {{"library", "chip"}, {"cell", "top"}, {"view", "layout"}, {"x", 1050}, {"y", 25}}).ok, "a hidden layer is not selectable");
        check(!call("layout_view_layer", {{"library", "chip"}, {"cell", "top"}, {"view", "layout"}, {"layer", 99}, {"datatype", 0}, {"visible", true}}).ok, "an absent layer is an error");
        check(call("layout_view_levels", {{"library", "chip"}, {"cell", "top"}, {"view", "layout"}, {"levels", 0}}).ok && v->flat().shapes.empty(), "levels redraw");
        check(call("ui_theme", {{"mode", "light"}}).ok, "the viewer follows the theme");
    }
    QSettings().clear();
    fs::remove_all(dir);
    std::printf(g_fail ? "FAILED (%d)\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
