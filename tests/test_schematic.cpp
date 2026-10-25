// test_schematic: the schematic and symbol model, through its commands. Primitive NMOS/PMOS symbols, an inverter
// schematic (ports, T-junctions, labelled supply stubs joined by name) whose connectivity is exactly the inverter's;
// orientations; crossing wires that do not connect; shorts, labels on nothing, floating pins, missing masters;
// symbol generation; undo.
#include "cmd/registry.h"
#include "oadb/oadb.h"
#include "sch/schematic.h"

#include <QJsonDocument>

#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <unistd.h>

static int g_fail = 0;
static void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++g_fail;
}
static std::string str(const QJsonValue& v) {
    return QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact).toStdString();
}
static QJsonArray xy(int64_t x, int64_t y) { return QJsonArray{(double)x, (double)y}; }

int main() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("sch_test_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);

    // orientations: (2, 1) under each
    {
        const std::vector<std::pair<std::string, oadb::Point>> want = {
            {"R0", {2, 1}}, {"R90", {-1, 2}}, {"R180", {-2, -1}}, {"R270", {1, -2}},
            {"MY", {-2, 1}}, {"MX", {2, -1}}, {"MYR90", {-1, -2}}, {"MXR90", {1, 2}}};
        bool all = true;
        for (const auto& [o, p] : want) all = all && sch::transform(o, {2, 1}) == p;
        check(all, "the eight orientations place (2, 1) as Virtuoso does");
    }

    cmd::Registry r;
    cmd::addCoreCommands(r);
    cmd::addSchematicCommands(r);
    auto ws = oadb::Workspace::load((dir / "openvlsi.libs").string());
    cmd::Context ctx{ws};
    bool failed = false;
    auto call = [&](const char* name, QJsonObject a) {
        auto res = r.call(ctx, name, a);
        if (!res.ok) std::printf("    (%s: %s)\n", name, res.error.toStdString().c_str());
        return res;
    };
    auto must = [&](const char* name, QJsonObject a) {  // a step the test relies on: its failure fails the test
        auto res = call(name, a);
        failed = failed || !res.ok;
        if (!res.ok) check(false, std::string(name) + " must succeed: " + res.error.toStdString());
        return res.value;
    };

    // primitives: MOS symbols, D/S on top and bottom, G left, B right; instNamePrefix M
    must("library_create", {{"library", "prims"}, {"path", "prims.oalib"}});
    for (const char* dev : {"nmos", "pmos"}) {
        const bool p = std::string(dev) == "pmos";
        must("cell_create", {{"library", "prims"}, {"cell", dev}});
        must("view_create", {{"library", "prims"}, {"cell", dev}, {"view", "symbol"}, {"type", "symbol"}});
        must("cell_prop_set", {{"library", "prims"}, {"cell", dev}, {"key", "instNamePrefix"}, {"value", "M"}});
        const QJsonObject sym{{"library", "prims"}, {"cell", dev}, {"view", "symbol"}};
        auto pin = [&](const char* n, int64_t x, int64_t y) {
            QJsonObject a = sym;
            a["name"] = n; a["direction"] = "inputOutput"; a["x"] = (double)x; a["y"] = (double)y;
            must("pin_add", a);
        };
        pin(p ? "S" : "D", 0, 100);
        pin(p ? "D" : "S", 0, -100);
        pin("G", -100, 0);
        pin("B", 100, 0);
        QJsonObject box = sym;
        box["kind"] = "rect"; box["points"] = QJsonArray{xy(-50, -100), xy(50, 100)};
        must("shape_add", box);
    }
    check(!failed, "primitive nmos/pmos symbols: pins, a body, instNamePrefix");

    // the inverter
    must("library_create", {{"library", "analog"}, {"path", "analog.oalib"}});
    must("cell_create", {{"library", "analog"}, {"cell", "inv"}});
    must("view_create", {{"library", "analog"}, {"cell", "inv"}, {"view", "schematic"}, {"type", "schematic"}});
    const QJsonObject sc{{"library", "analog"}, {"cell", "inv"}, {"view", "schematic"}};
    auto with = [&](QJsonObject extra) { QJsonObject a = sc; for (auto it = extra.begin(); it != extra.end(); ++it) a[it.key()] = it.value(); return a; };
    auto inst = [&](const char* name, const char* dev, int64_t x, int64_t y, const char* orient = "R0") {
        QJsonObject e{{"master_library", "prims"}, {"master_cell", dev}, {"x", (double)x}, {"y", (double)y}, {"orient", orient}};
        if (name) e["name"] = name;
        return call("instance_add", with(e));
    };
    auto wire = [&](QJsonArray pts) { return must("wire_add", with({{"points", pts}})); };
    auto label = [&](const char* n, int64_t x, int64_t y) { return call("label_add", with({{"name", n}, {"x", (double)x}, {"y", (double)y}})); };
    failed = false;
    must("pin_add", with({{"name", "A"}, {"direction", "input"}, {"x", -300.}, {"y", 150.}}));
    must("pin_add", with({{"name", "Y"}, {"direction", "output"}, {"x", 300.}, {"y", 150.}}));
    failed = failed || !inst("MP", "pmos", 0, 300).ok || !inst("MN", "nmos", 0, 0).ok;
    wire({xy(0, 200), xy(0, 100)});                 // drain to drain
    wire({xy(0, 150), xy(300, 150)});               // T into it, out to Y
    wire({xy(-100, 300), xy(-100, 0)});             // gate to gate
    wire({xy(-300, 150), xy(-100, 150)});           // A, T into the gates
    wire({xy(0, 400), xy(0, 500)});                 // PMOS source stub
    wire({xy(100, 300), xy(100, 450)});             // PMOS bulk stub
    wire({xy(0, -100), xy(0, -200)});               // NMOS source stub
    wire({xy(100, 0), xy(100, -150)});              // NMOS bulk stub
    failed = failed || !label("vdd!", 0, 450).ok || !label("vdd!", 100, 400).ok || !label("gnd!", 0, -150).ok || !label("gnd!", 100, -100).ok;
    check(!failed, "the inverter schematic: ports, two instances, wires, supply labels");

    auto check1 = must("schematic_check", sc).toObject();
    std::map<std::string, std::string> nets;  // name -> its terminals, as text
    for (const auto& n : check1["nets"].toArray()) {
        std::string t;
        for (const auto& x : n.toObject()["terminals"].toArray())
            t += (t.empty() ? "" : " ") + x.toObject()["instance"].toString().toStdString() + "." + x.toObject()["pin"].toString().toStdString();
        nets[n.toObject()["name"].toString().toStdString()] = t;
    }
    const std::map<std::string, std::string> want = {
        {"A", ".A MN.G MP.G"}, {"Y", ".Y MN.D MP.D"}, {"vdd!", "MP.B MP.S"}, {"gnd!", "MN.B MN.S"}};
    check(check1["ok"].toBool() && nets == want && check1["warnings"].toArray().isEmpty(),
          "connectivity is the inverter's: A, Y, vdd!, gnd! (" + str(check1) + ")");

    // symbol generation; then the symbol matches the schematic
    check(must("symbol_generate", {{"library", "analog"}, {"cell", "inv"}}).toString() == "symbol", "symbol_generate");
    auto sym = must("view_get", {{"library", "analog"}, {"cell", "inv"}, {"view", "symbol"}}).toObject();
    const auto sp = sym["pins"].toArray();
    check(sp.size() == 2 && sp[0].toObject()["name"] == "A" && sp[0].toObject()["direction"] == "input" &&
              sp[0].toObject()["at"].toArray()[0].toDouble() < 0 && sp[1].toObject()["name"] == "Y" &&
              sp[1].toObject()["at"].toArray()[0].toDouble() > 0,
          "the symbol has A on the left (input) and Y on the right (output)");
    check(!call("symbol_generate", {{"library", "analog"}, {"cell", "inv"}}).ok, "generating over an existing symbol is an error");
    std::map<QString, QString> just;
    for (const auto& h : sym["shapes"].toArray())
        if (h.toObject()["kind"] == "label") just[h.toObject()["text"].toString()] = h.toObject()["justify"].toString();
    check(just["A"] == "centerLeft" && just["Y"] == "centerRight" && just["[@cellName]"] == "centerCenter",
          "the generated labels are justified away from their pins");

    // a floating instance: auto-named by its prefix, four floating pins; then undone
    auto m = inst(nullptr, "nmos", 800, 800, "R90");
    auto check2 = must("schematic_check", sc).toObject();
    int floating = 0;
    for (const auto& w : check2["warnings"].toArray()) floating += w.toString().contains("M0 pin") ? 1 : 0;
    check(m.ok && m.value.toString() == "M0" && check2["ok"].toBool() && floating == 4,
          "an unconnected instance (auto-named M0) warns of its four floating pins");
    check(call("edit_undo", {{"library", "analog"}}).ok &&
              must("view_get", sc).toObject()["instances"].toArray().size() == 2, "undo removes it again");

    // an instance's parameters go and come back with it
    must("instance_set_params", with({{"instance", "MP"}, {"params", QJsonObject{{"w", "2u"}, {"l", "180n"}}}}));
    must("objects_delete", with({{"objects", QJsonArray{QJsonObject{{"kind", "instance"}, {"key", "MP"}}}}}));
    const bool gone = must("view_get", sc).toObject()["instances"].toArray().size() == 1;
    must("edit_undo", {{"library", "analog"}});
    QJsonObject mp;
    for (const auto& i : must("view_get", sc).toObject()["instances"].toArray()) if (i.toObject()["name"] == "MP") mp = i.toObject();
    check(gone && mp["params"].toObject() == QJsonObject{{"w", "2u"}, {"l", "180n"}},
          "deleting an instance takes its parameters; undo brings both back (" + str(mp) + ")");

    // crossing wires do not connect
    wire({xy(400, -50), xy(600, -50)});
    wire({xy(500, -150), xy(500, 50)});
    label("c1", 450, -50);
    label("c2", 500, 0);
    auto check3 = must("schematic_check", sc).toObject();
    bool c1 = false, c2 = false;
    for (const auto& n : check3["nets"].toArray()) { c1 = c1 || n.toObject()["name"] == "c1"; c2 = c2 || n.toObject()["name"] == "c2"; }
    check(check3["ok"].toBool() && c1 && c2, "crossing wires stay two nets (c1, c2)");

    // objects move together, as one undo step; a move naming a missing object moves nothing
    {
        auto exported = [&] { return must("library_export", {{"library", "analog"}}).toString().toStdString(); };
        const std::string before = exported();
        const auto v = must("view_get", sc).toObject();
        QJsonArray objs;
        for (const auto& w : v["wires"].toArray())
            if (w.toObject()["points"].toArray()[0].toArray() == xy(400, -50)) objs.append(QJsonObject{{"kind", "wire"}, {"key", w.toObject()["id"]}});
        for (const auto& l : v["labels"].toArray())
            if (l.toObject()["name"] == "c1") objs.append(QJsonObject{{"kind", "label"}, {"key", l.toObject()["id"]}});
        must("objects_move", with({{"objects", objs}, {"dx", 0.}, {"dy", -1000.}}));
        const std::string moved = exported();
        check(objs.size() == 2 && moved.find("wire (400 -1050) (600 -1050)") != std::string::npos &&
                  moved.find("label c1 (450 -1050)") != std::string::npos && must("schematic_check", sc).toObject()["ok"].toBool(),
              "objects_move moves a wire and its label together (the label still names it)");
        objs.append(QJsonObject{{"kind", "wire"}, {"key", 99999}});
        check(!call("objects_move", with({{"objects", objs}, {"dx", 5.}, {"dy", 0.}})).ok && exported() == moved,
              "a move naming a missing object moves nothing");
        check(call("edit_undo", {{"library", "analog"}}).ok && exported() == before, "one undo returns them all");
    }

    // errors: a short, a label on nothing
    label("Y2", 200, 150);
    label("orphan", 1000, 1000);
    auto check4 = must("schematic_check", sc).toObject();
    const std::string errs = str(check4["errors"]);
    check(!check4["ok"].toBool() && errs.find("short") != std::string::npos && errs.find("Y, Y2") != std::string::npos &&
              errs.find("orphan") != std::string::npos,
          "a second name on Y is a short, a label on nothing an error (" + errs + ")");

    // validation at the commands
    check(!inst("BAD", "nosuchcell", 0, 0).ok, "an instance of a missing master is refused");
    check(!call("instance_add", with({{"master_library", "prims"}, {"master_cell", "nmos"}, {"x", 0.}, {"y", 0.}, {"orient", "R45"}})).ok,
          "an orientation outside the eight is refused");
    check(!call("wire_add", with({{"points", QJsonArray{xy(0, 0)}}})).ok, "a one-point wire is refused");
    check(!call("pin_add", {{"library", "prims"}, {"cell", "nmos"}, {"view", "symbol"}, {"name", "G"}, {"direction", "input"}, {"x", 0.}, {"y", 0.}}).ok,
          "a duplicate pin name is refused");
    check(!call("wire_add", {{"library", "prims"}, {"cell", "nmos"}, {"view", "symbol"}, {"points", QJsonArray{xy(0, 0), xy(1, 0)}}}).ok,
          "a symbol holds no wires");

    // the library text shows the schematic
    const std::string text = must("library_export", {{"library", "analog"}}).toString().toStdString();
    check(text.find("instance MP prims/pmos/symbol (0 300) R0") != std::string::npos && text.find("label vdd! (0 450)") != std::string::npos,
          "library_export lists instances, wires and labels");

    fs::remove_all(dir);
    std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "OK", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
