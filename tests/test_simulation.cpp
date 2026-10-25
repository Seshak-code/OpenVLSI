// test_simulation: the netlister and TreeSPICE runs, through the commands. Primitive symbols netlist by their spice
// templates and parameter defaults; a schematic cell is a .subckt with globals and ground; a config's view list and
// bindings switch a cell between primitive and schematic; op, dc, ac and tran results come back right (a divider, an
// inverter's transfer, an RC pole). Every way a netlist would be wrong is an error, as are bad cards, a failing
// simulator and a run outside the workspace.
#include "cmd/registry.h"
#include "oadb/oadb.h"

#include <QJsonDocument>

#include <cmath>
#include <complex>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>
#include <unistd.h>

static int g_fail = 0;
static void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++g_fail;
}

int main() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("sim test " + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);

    cmd::Registry r;
    cmd::addCoreCommands(r);
    cmd::addSchematicCommands(r);
    cmd::addSimulationCommands(r);
    auto ws = oadb::Workspace::load((dir / "openvlsi.libs").string());
    cmd::Context ctx{ws, OPENVLSI_TREESPICE};
    auto call = [&](const char* name, QJsonObject a) { return r.call(ctx, name, a); };
    auto must = [&](const char* name, QJsonObject a) {
        auto res = call(name, a);
        if (!res.ok) check(false, std::string(name) + " must succeed: " + res.error.toStdString());
        return res.value;
    };
    auto error = [&](const char* name, QJsonObject a) {  // the call's error ("" when it succeeded)
        auto res = call(name, a);
        return res.ok ? std::string() : res.error.toStdString();
    };
    auto has = [](const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; };

    must("library_create", {{"library", "prims"}, {"path", "prims.oalib"}});
    must("library_create", {{"library", "sims"}, {"path", "sims.oalib"}});

    // primitives: a symbol's pins, its spice template and parameter defaults
    struct P { const char* name; const char* dir; int x, y; };
    auto primitive = [&](const char* cell, std::vector<P> pins, const char* prefix, const char* tmpl, QJsonObject params) {
        must("cell_create", {{"library", "prims"}, {"cell", cell}});
        must("view_create", {{"library", "prims"}, {"cell", cell}, {"view", "symbol"}, {"type", "symbol"}});
        for (const auto& p : pins)
            must("pin_add", {{"library", "prims"}, {"cell", cell}, {"view", "symbol"}, {"name", p.name}, {"direction", p.dir}, {"x", p.x}, {"y", p.y}});
        must("cell_prop_set", {{"library", "prims"}, {"cell", cell}, {"key", "spice"}, {"value", QJsonObject{{"prefix", prefix}, {"template", tmpl}}}});
        if (!params.isEmpty()) must("cell_prop_set", {{"library", "prims"}, {"cell", cell}, {"key", "params"}, {"value", params}});
    };
    const std::vector<P> two = {{"PLUS", "inputOutput", 0, 100}, {"MINUS", "inputOutput", 0, -100}};
    const std::vector<P> mos = {{"D", "inputOutput", 0, 100}, {"G", "input", -100, 0}, {"S", "inputOutput", 0, -100}, {"B", "inputOutput", 100, 0}};
    primitive("res", two, "R", "[|PLUS] [|MINUS] [@r]", {{"r", "1k"}});
    primitive("cap", two, "C", "[|PLUS] [|MINUS] [@c]", {});
    primitive("vdc", two, "V", "[|PLUS] [|MINUS] dc [@dc] [@ac:ac %]", {{"dc", "0"}});
    primitive("nmos", mos, "M", "[|D] [|G] [|S] [|B] [@model] [@w:w=%] [@l:l=%]", {{"model", "nch"}, {"w", "1u"}, {"l", "1u"}});
    primitive("pmos", mos, "M", "[|D] [|G] [|S] [|B] [@model] [@w:w=%] [@l:l=%]", {{"model", "pch"}, {"w", "2u"}, {"l", "1u"}});

    // a schematic: instances whose pins get nets by labels placed on them
    auto schematic = [&](const char* cell) {
        must("cell_create", {{"library", "sims"}, {"cell", cell}});
        must("view_create", {{"library", "sims"}, {"cell", cell}, {"view", "schematic"}, {"type", "schematic"}});
    };
    auto inst = [&](const char* cell, const char* name, const char* master, int x, int y, std::vector<const char*> nets, QJsonObject params = {},
                    const char* masterLib = "prims") {
        QJsonObject a{{"library", "sims"}, {"cell", cell}, {"view", "schematic"}, {"master_library", masterLib}, {"master_cell", master},
                      {"x", x}, {"y", y}, {"params", params}};
        if (name) a["name"] = name;
        const QString n = must("instance_add", a).toString();
        const auto pins = must("view_get", {{"library", masterLib}, {"cell", master}, {"view", "symbol"}}).toObject()["pins"].toArray();
        for (int k = 0; k < (int)nets.size() && k < pins.size(); ++k) {  // nets in the symbol's (name-sorted) pin order
            const QJsonArray at = pins[k].toObject()["at"].toArray();
            must("label_add", {{"library", "sims"}, {"cell", cell}, {"view", "schematic"}, {"name", nets[k]},
                               {"x", x + at[0].toInt()}, {"y", y + at[1].toInt()}});
        }
        return n;
    };
    auto sc = [](const char* cell, const char* view = "schematic") { return QJsonObject{{"library", "sims"}, {"cell", cell}, {"view", view}}; };
    auto with = [](QJsonObject a, QJsonObject extra) { for (auto it = extra.begin(); it != extra.end(); ++it) a[it.key()] = it.value(); return a; };
    auto vec = [](const QJsonObject& plot, const QString& name) {  // a vector's name as the simulator spelled it
        for (const auto& v : plot["vectors"].toArray()) if (v.toString().compare(name, Qt::CaseInsensitive) == 0) return v.toString();
        return QString();
    };

    // ── a divider: primitives, defaults, ground; op and tran ──
    schematic("div");
    inst("div", "V0", "vdc", 0, 0, {"gnd!", "in"}, {{"dc", "1"}});      // pins MINUS, PLUS
    inst("div", "R0", "res", 500, 0, {"out", "in"});                    // r from the cell's default
    inst("div", "R1", "res", 1000, 0, {"gnd!", "out"}, {{"r", "3k"}});
    const std::string div = must("netlist", sc("div")).toString().toStdString();
    check(has(div, "* sims/div/schematic (OpenVLSI)\n") && has(div, "V0 in 0 dc 1\n") && has(div, "R0 in out 1k\n") &&
              has(div, "R1 out 0 3k\n") && !has(div, ".global"),
          "primitives netlist by template and default; gnd! is node 0 (" + div + ")");
    const QJsonObject run = must("simulate", with(sc("div"), {{"cards", QJsonArray{".op", ".tran 1n 10n"}}})).toObject();
    const QJsonArray plots = run["plots"].toArray();
    const QJsonObject op = plots[0].toObject();
    const double vout = op["values"].toObject()[vec(op, "v(out)")].toDouble();
    check(plots.size() == 2 && op["name"] == "Operating Point" && std::abs(vout - 0.75) < 1e-9,
          "the divider's operating point: v(out) = 0.75 (" + std::to_string(vout) + ")");
    check(plots[1].toObject()["name"] == "Transient Analysis" && plots[1].toObject()["vectors"].toArray()[0] == "time" &&
              plots[1].toObject()["points"].toInt() > 2,
          "a second analysis is a second plot, with its scale first");
    const std::string runDir = run["run"].toString().toStdString();
    check(fs::exists(fs::path(runDir) / "input.cir") && fs::exists(fs::path(runDir) / "log.txt") && has(runDir, "simulation") &&
              runDir.substr(runDir.size() - 4) == "run1",
          "the run keeps its netlist and log under the workspace (" + runDir + ")");
    const QJsonObject tran = must("results_get", {{"run", run["run"]}, {"plot", 1}, {"vectors", QJsonArray{"time", vec(plots[1].toObject(), "v(out)")}}}).toObject();
    const QJsonArray vo = tran["vectors"].toObject()[vec(plots[1].toObject(), "v(out)")].toArray();
    check(tran["vectors"].toObject().size() == 2 && !vo.isEmpty() && std::abs(vo.last().toDouble() - 0.75) < 1e-9,
          "results_get returns just the named vectors (v(out) stays 0.75)");
    const QJsonObject again = must("simulate", with(sc("div"), {{"cards", QJsonArray{".op"}}})).toObject();
    check(again["run"].toString().endsWith("run2"), "each run gets its own directory");

    // ── an inverter cell, a .subckt with globals, swept ──
    schematic("inv");
    for (auto [name, dir, x] : {std::tuple{"A", "input", -500}, std::tuple{"Y", "output", 500}})
        must("pin_add", with(sc("inv"), {{"name", name}, {"direction", dir}, {"x", x}, {"y", 0}}));
    inst("inv", "MN", "nmos", 0, -300, {"gnd!", "Y", "A", "gnd!"});  // B D G S
    inst("inv", "MP", "pmos", 0, 300, {"vdd!", "Y", "A", "vdd!"});
    must("symbol_generate", {{"library", "sims"}, {"cell", "inv"}});
    schematic("invtb");
    inst("invtb", "I0", "inv", 0, 0, {"in", "out"}, {}, "sims");
    inst("invtb", "VDD", "vdc", -1000, 0, {"gnd!", "vdd!"}, {{"dc", "1.8"}});
    inst("invtb", "VIN", "vdc", -1500, 0, {"gnd!", "in"});
    const std::string inv = must("netlist", sc("invtb")).toString().toStdString();
    check(has(inv, ".global vdd!\n") && has(inv, ".subckt inv A Y\n") && has(inv, "MN Y A 0 0 nch w=1u l=1u\n") &&
              has(inv, "MP Y A vdd! vdd! pch w=2u l=1u\n") && has(inv, ".ends inv\n") && has(inv, "XI0 in out inv\n") &&
              has(inv, "VDD vdd! 0 dc 1.8\n"),
          "a schematic cell is a .subckt; vdd! is global; names get their element letter unless they start with it (" + inv + ")");
    const QJsonObject models{{"cards", QJsonArray{".model nch nmos level=1 vto=0.5 kp=100u", ".model pch pmos level=1 vto=-0.5 kp=50u",
                                                  ".dc VIN 0 1.8 0.1"}}};
    const QJsonObject dc = must("simulate", with(sc("invtb"), models)).toObject()["plots"].toArray()[0].toObject();
    const QJsonArray sweep = must("results_get", {{"run", must("simulate", with(sc("invtb"), models)).toObject()["run"]}, {"plot", 0}})
                                 .toObject()["vectors"].toObject()[vec(dc, "v(out)")].toArray();
    check(sweep.size() == 19 && sweep.first().toDouble() > 1.75 && sweep.last().toDouble() < 0.05,
          "the inverter's DC transfer: out high at in = 0, low at in = 1.8 (" + std::to_string(sweep.first().toDouble()) + " .. " +
              std::to_string(sweep.last().toDouble()) + ")");

    // ── an RC pole: AC, complex ──
    schematic("rc");
    inst("rc", "V0", "vdc", 0, 0, {"gnd!", "in"}, {{"ac", "1"}});
    inst("rc", "R0", "res", 500, 0, {"out", "in"});
    inst("rc", "C0", "cap", 1000, 0, {"gnd!", "out"}, {{"c", "1n"}});
    const double f3 = 1 / (2 * M_PI * 1e3 * 1e-9);
    const QJsonObject ac = must("simulate", with(sc("rc"), {{"cards", QJsonArray{QString(".ac lin 1 %1 %1").arg(f3, 0, 'g', 12)}}})).toObject();
    const QJsonObject acPlot = ac["plots"].toArray()[0].toObject();
    const QJsonObject h = must("results_get", {{"run", ac["run"]}, {"plot", 0}}).toObject()["vectors"].toObject()[vec(acPlot, "v(out)")].toObject();
    const double mag = std::abs(std::complex<double>(h["re"].toArray()[0].toDouble(), h["im"].toArray()[0].toDouble()));
    check(acPlot["complex"].toBool() && std::abs(mag - 1 / std::sqrt(2.0)) < 1e-6, "an RC at its pole: |v(out)| = 1/√2 (" + std::to_string(mag) + ")");

    // ── a config: view list and bindings switch a cell between its spice and its schematic ──
    must("cell_create", {{"library", "sims"}, {"cell", "rload"}});
    must("view_create", {{"library", "sims"}, {"cell", "rload"}, {"view", "schematic"}, {"type", "schematic"}});
    for (auto [name, y] : {std::pair{"P", 300}, std::pair{"N", -300}})
        must("pin_add", with(sc("rload"), {{"name", name}, {"direction", "inputOutput"}, {"x", -500}, {"y", y}}));
    inst("rload", "R0", "res", 0, 200, {"mid", "P"});
    inst("rload", "R1", "res", 0, -200, {"N", "mid"});
    must("symbol_generate", {{"library", "sims"}, {"cell", "rload"}});
    must("cell_prop_set", {{"library", "sims"}, {"cell", "rload"}, {"key", "spice"}, {"value", QJsonObject{{"prefix", "R"}, {"template", "[|P] [|N] 3k"}}}});
    schematic("cfgtb");
    inst("cfgtb", "V0", "vdc", 0, 0, {"gnd!", "in"}, {{"dc", "1"}});
    inst("cfgtb", "R9", "res", 500, 0, {"out", "in"});
    inst("cfgtb", nullptr, "rload", 1000, 0, {"gnd!", "out"}, {}, "sims");  // pins N, P
    auto voutOf = [&](const char* view) {
        const QJsonObject o = must("simulate", with(sc("cfgtb", view), {{"cards", QJsonArray{".op"}}})).toObject()["plots"].toArray()[0].toObject();
        return o["values"].toObject()[vec(o, "v(out)")].toDouble();
    };
    const std::string flat = must("netlist", sc("cfgtb")).toString().toStdString();
    check(has(flat, "RI0 out 0 3k\n") && !has(flat, ".subckt") && std::abs(voutOf("schematic") - 0.75) < 1e-9,
          "by default a cell with a spice property is a primitive (" + flat + ")");
    for (auto [view, props] : {std::pair{"config", QJsonObject{{"viewList", QJsonArray{"schematic", "spice"}}}},
                               std::pair{"bound", QJsonObject{{"bindings", QJsonObject{{"sims/rload", "schematic"}}}}}}) {
        must("view_create", {{"library", "sims"}, {"cell", "cfgtb"}, {"view", view}, {"type", "config"}});
        must("prop_set", {{"library", "sims"}, {"cell", "cfgtb"}, {"view", view}, {"key", "top"}, {"value", "sims/cfgtb/schematic"}});
        for (auto it = props.begin(); it != props.end(); ++it)
            must("prop_set", {{"library", "sims"}, {"cell", "cfgtb"}, {"view", view}, {"key", it.key()}, {"value", it.value()}});
        const std::string text = must("netlist", sc("cfgtb", view)).toString().toStdString();
        check(has(text, ".subckt rload N P\n") && has(text, "XI0 0 out rload\n") && std::abs(voutOf(view) - 2.0 / 3) < 1e-9,
              std::string("a config's ") + (std::string(view) == "config" ? "view list" : "binding") + " descends into rload's schematic: v(out) = 2/3");
    }
    must("prop_set", {{"library", "sims"}, {"cell", "cfgtb"}, {"view", "bound"}, {"key", "bindings"}, {"value", QJsonObject{{"sims/rload", "layout"}}}});
    check(has(error("netlist", sc("cfgtb", "bound")), "bound to 'layout'"), "a binding to a view the cell lacks is an error");

    // ── what would make a wrong netlist is an error ──
    auto refused = [&](const char* cell, std::function<void()> build, const std::string& why, const char* what) {
        schematic(cell);
        build();
        const std::string e = error("netlist", sc(cell));
        check(has(e, why), std::string(what) + " (" + e + ")");
    };
    refused("nocap", [&] { inst("nocap", "C1", "cap", 0, 0, {"a", "b"}); }, "no value for parameter c", "a parameter with no value");
    refused("localgnd", [&] { inst("localgnd", "R1", "res", 0, 0, {"gnd", "x"}); }, "would be SPICE's ground", "a local net named gnd");
    refused("cases", [&] { inst("cases", "R1", "res", 0, 0, {"a", "A"}); }, "ignores case", "nets SPICE would merge (a, A)");
    refused("short", [&] {
        inst("short", "R1", "res", 0, 0, {"a", "b"});
        must("label_add", with(sc("short"), {{"name", "c"}, {"x", 0}, {"y", 100}}));
    }, "does not check", "a schematic that does not check (a short)");
    refused("elements", [&] {
        inst("elements", "RX", "res", 0, 0, {"a", "b"});
        inst("elements", "rx", "res", 500, 0, {"c", "d"});
    }, "one element to SPICE", "two instances SPICE would merge (RX, rx)");
    refused("spaces", [&] { inst("spaces", "R1", "res", 0, 0, {"a", "b"}, {{"r", "1k .include x"}}); }, "holds spaces",
            "a parameter value with spaces (a second card in disguise)");
    schematic("loop");
    must("pin_add", with(sc("loop"), {{"name", "A"}, {"direction", "input"}, {"x", 0}, {"y", 0}}));
    must("symbol_generate", {{"library", "sims"}, {"cell", "loop"}});
    inst("loop", nullptr, "loop", 1000, 0, {"A"}, {}, "sims");
    refused("looptb", [&] { inst("looptb", nullptr, "loop", 0, 0, {"x"}, {}, "sims"); }, "contains itself", "a schematic that contains itself");
    must("pin_add", {{"library", "sims"}, {"cell", "rload"}, {"view", "symbol"}, {"name", "Q"}, {"direction", "input"}, {"x", 900}, {"y", 900}});
    check(has(error("netlist", sc("cfgtb", "config")), "differ"), "a symbol whose pins differ from its schematic's");

    check(has(error("simulate", with(sc("div"), {{"cards", QJsonArray{"R9 a b 1k"}}})), "one line starting with") &&
              has(error("simulate", with(sc("div"), {{"cards", QJsonArray{".op\n.include /etc/passwd"}}})), "one line") &&
              has(error("simulate", with(sc("div"), {{"cards", QJsonArray{".options reltol=1e-4"}}})), "no analysis"),
          "cards: an element, a second line, no analysis are refused");
    check(has(error("simulate", with(sc("invtb"), {{"cards", QJsonArray{".op"}}})), "the simulation failed"),
          "a failing simulation (no MOS models) is an error with its log");
    {
        const std::string e = error("simulate", with(sc("div"), {{"cards", QJsonArray{".dc VNOPE 0 1 0.5"}}}));
        check(has(e, "VNOPE") && has(e, "not found"), "a failed analysis is an error that quotes the simulator (" + e + ")");
        const std::string e2 = error("simulate", with(sc("invtb"), {{"cards", QJsonArray{".model nch nmos level=1", ".model pch pmos level=1", ".dc VVIN 0 1.8 0.1"}}}));
        check(has(e2, "VVIN") && has(e2, "not found"), "the same with models and a subcircuit (" + e2 + ")");
    }
    check(has(error("results_get", {{"run", QString::fromStdString(dir.string())}, {"plot", 0}}), "no run") &&
              has(error("results_get", {{"run", run["run"]}, {"plot", 7}}), "no plot 7"),
          "results_get refuses a directory outside the simulations, and a missing plot");
    cmd::Context none{ws};
    check(!r.call(none, "simulate", with(sc("div"), {{"cards", QJsonArray{".op"}}})).ok, "no simulator is an error");

    fs::remove_all(dir);
    std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "OK", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
