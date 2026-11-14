// The Verify stage: Pegasus's role. DRC and LVS of a layout view through its process's own KLayout decks, and what they found, in
// a form the layout viewer can walk. Runs are directories (verification/<library>_<cell>/<kind>.N: run.json, deck.log, reports).
#include "cmd/registry.h"
#include "flow/rtl_flow.h"
#include "flow/subprocess.h"
#include "layout/tech.h"
#include "oadb/oadb.h"
#include "sch/schematic.h"
#include "sim/ade.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QXmlStreamReader>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace cmd {

namespace {

namespace fs = std::filesystem;
using namespace acutesim::compute::orchestration;

const QJsonObject kLib = param("string", "library name"), kCell = param("string", "cell name"), kView = param("string", "the layout view (default \"layout\")");
const QJsonObject kRun = param("string", "a verification run: <library>_<cell>/drc.N or lvs.N");

std::string s(const QJsonObject& a, const char* key) { return a[key].toString().toStdString(); }
QString q(const std::string& text) { return QString::fromStdString(text); }
std::string viewOf(const QJsonObject& a) { return a.contains("view") ? s(a, "view") : "layout"; }
double now() { return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count(); }
fs::path base(Context& c) { return fs::absolute(fs::path(c.workspace.file()).parent_path()); }

sim::Jobs& jobs(Context& c) {
    if (!c.jobs) c.jobs = std::make_shared<sim::Jobs>();
    return *c.jobs;
}

QJsonObject readJson(const fs::path& file) {
    QFile f(QString::fromStdString(file.string()));
    if (!f.open(QIODevice::ReadOnly)) throw std::runtime_error("cannot read " + file.string());
    QJsonParseError e;
    const QJsonDocument d = QJsonDocument::fromJson(f.readAll(), &e);
    if (e.error != QJsonParseError::NoError) throw std::runtime_error(file.string() + ": " + e.errorString().toStdString());
    return d.object();
}

void writeRecord(const fs::path& dir, const QJsonObject& rec) {
    const fs::path tmp = dir / "run.json.tmp";
    std::ofstream(tmp) << QJsonDocument(rec).toJson().toStdString();
    fs::rename(tmp, dir / "run.json");
}

fs::path runPath(Context& c, const QJsonObject& a) {
    static const QRegularExpression name("^[A-Za-z0-9_]+/(drc|lvs)\\.[0-9]+$");
    if (!name.match(a["run"].toString()).hasMatch()) throw std::invalid_argument("a verification run is named <library>_<cell>/drc.N or lvs.N");
    const fs::path d = base(c) / "verification" / s(a, "run");
    if (!fs::is_directory(d)) throw std::invalid_argument("no verification run " + s(a, "run"));
    return d;
}

fs::path newRun(Context& c, const std::string& library, const std::string& cell, const std::string& kind, std::string* name) {
    const fs::path dir = base(c) / "verification" / (library + "_" + cell);
    fs::create_directories(dir);
    int n = 0;
    for (const auto& e : fs::directory_iterator(dir)) {
        const std::string f = e.path().filename().string();
        if (f.rfind(kind + ".", 0) == 0) n = std::max(n, std::atoi(f.c_str() + kind.size() + 1));
    }
    while (!fs::create_directory(dir / (kind + "." + std::to_string(++n)))) {}
    *name = library + "_" + cell + "/" + kind + "." + std::to_string(n);
    return dir / (kind + "." + std::to_string(n));
}

const flow::Platform& platformOf(const oadb::Library& l) {
    const auto tech = l.libraryProp("tech");
    if (!tech) throw std::invalid_argument("library " + l.name() + " has no process attached (layout_tech_set)");
    const std::string name = QJsonDocument::fromJson(QByteArray::fromStdString("[" + *tech + "]")).array()[0].toString().toStdString();
    for (const auto& p : flow::platforms())
        if (p.tech == name) return p;
    throw std::invalid_argument("process " + name + " has no decks here");
}

// A process run from a deck: its log into deck.log. False with the reason.
bool runDeck(const RtlFlowTools& tools, const fs::path& dir, const std::vector<std::string>& args, const std::atomic<bool>* cancel, std::string* why) {
    acutesim::orchestration::Subprocess p(tools.klayout, args);
    p.setWorkingDirectory(dir.string());
    if (!tools.binDir.empty()) {
        const char* path = std::getenv("PATH");
        p.setEnvironment({{"PATH", tools.binDir + ":" + (path ? path : "")}});
    }
    if (cancel) p.setCancelFlag(cancel);
    const auto r = p.run();
    std::ofstream(dir / "deck.log") << r.stdout_output << r.stderr_output;
    if (r.exit_code != 0) {
        *why = "the deck stopped with exit " + std::to_string(r.exit_code) + "; see its log";
        return false;
    }
    return true;
}

// A SPICE value in a schematic's parameter, as CDL takes it: passed through once it reads as a number with a unit
std::string plainValue(const std::string& value, const std::string& what) {
    char* end = nullptr;
    std::strtod(value.c_str(), &end);
    if (end == value.c_str()) throw std::invalid_argument(what + " '" + value + "' is an expression; LVS needs its value");
    return value;
}

// The schematic as LVS's reference: its transistors, on the deck's model names. Fails closed on anything else with a mask.
std::string schematicCdl(oadb::Workspace& ws, const std::string& lib, const std::string& cell, const std::string& view) {
    oadb::Library& l = ws.library(lib);
    const sch::Connectivity con = sch::extract(ws, lib, cell, view);
    if (!con.ok()) throw std::invalid_argument("the schematic has errors: " + con.errors.front());
    const auto net = [](std::string n) {
        if (!n.empty() && n.back() == '!') n.pop_back();
        return n;
    };
    std::ostringstream out;
    out << ".SUBCKT " << cell;
    for (const auto& p : l.pins(cell, view)) out << " " << net(p.name);
    out << "\n";
    for (const auto& i : l.instances(cell, view)) {
        if (i.cell != "sg13_lv_nmos" && i.cell != "sg13_lv_pmos") {
            if (i.cell == "vdc" || i.cell == "vsin" || i.cell == "vpulse" || i.cell == "gnd" || i.cell == "idc") continue;  // the testbench's: no mask
            throw std::invalid_argument("LVS has no reference for instance " + i.name + " of " + i.cell + " yet (sg13_lv_nmos and sg13_lv_pmos, in a flat schematic)");
        }
        const QJsonObject defaults = QJsonDocument::fromJson(QByteArray::fromStdString(ws.library(i.lib).cellProp(i.cell, "params").value_or("{}"))).object();
        const auto value = [&](const char* p) {
            const auto it = i.params.find(p);
            return plainValue(it != i.params.end() ? it->second : defaults[p].toVariant().toString().toStdString(), i.name + "'s " + p);
        };
        const auto nets = con.instanceNets.find(i.name);
        if (nets == con.instanceNets.end()) throw std::invalid_argument(i.name + " has no connections");
        const auto term = [&](const char* t) {
            const auto it = nets->second.find(t);
            if (it == nets->second.end()) throw std::invalid_argument(i.name + " has no net on " + t);
            return net(it->second);
        };
        out << "M" << i.name << " " << term("D") << " " << term("G") << " " << term("S") << " " << term("B") << " " << i.cell << " w=" << value("w") << " l=" << value("l")
            << " ng=" << value("ng") << " m=" << value("m") << "\n";
    }
    out << ".ENDS\n";
    return out.str();
}

// A KLayout report database as rules and their markers: {rules: [{rule, description, count, markers: [{cell, points: [[x, y], ...]}]}]}, microns in the marker's cell.
// ponytail: only the first 2000 markers of a rule are kept: past that a person needs the rule fixed, not a list.
QJsonObject readMarkers(const fs::path& lyrdb) {
    QFile f(QString::fromStdString(lyrdb.string()));
    if (!f.open(QIODevice::ReadOnly)) throw std::runtime_error("no report: " + lyrdb.string());
    QXmlStreamReader x(&f);
    std::map<QString, QString> descriptions;
    struct Rule { int count = 0; QJsonArray markers; };
    std::map<QString, Rule> rules;
    static const QRegularExpression pair("(-?[0-9.eE+-]+),(-?[0-9.eE+-]+)");
    QStringList path;
    QString name, description, category, cell, value;
    QJsonArray points;
    bool sawRoot = false;
    while (!x.atEnd()) {
        x.readNext();
        if (x.isStartElement()) {
            path << x.name().toString();
            sawRoot = true;
            const QString at = path.join('/');
            if (at == "report-database/items/item") {
                category.clear();
                cell.clear();
                points = {};
            } else if (at == "report-database/categories/category") {
                name.clear();
                description.clear();
            }
        } else if (x.isCharacters() && !x.isWhitespace()) {
            const QString at = path.join('/');
            const QString text = x.text().toString();
            if (at == "report-database/categories/category/name") name += text;
            else if (at == "report-database/categories/category/description") description += text;
            else if (at == "report-database/items/item/category") category += text;
            else if (at == "report-database/items/item/cell") cell += text;
            else if (at == "report-database/items/item/values/value") value += text;
        } else if (x.isEndElement()) {
            const QString at = path.join('/');
            if (at == "report-database/categories/category") descriptions[name] = description;
            else if (at == "report-database/items/item/values/value") {
                auto it = pair.globalMatch(value);
                while (it.hasNext()) {
                    const auto m = it.next();
                    points.append(QJsonArray{m.captured(1).toDouble(), m.captured(2).toDouble()});
                }
                value.clear();
            } else if (at == "report-database/items/item") {
                category.remove('\'');
                Rule& r = rules[category];
                ++r.count;
                if (r.markers.size() < 2000) r.markers.append(QJsonObject{{"cell", cell.remove('\'')}, {"points", points}});
            }
            if (!path.isEmpty()) path.removeLast();
        }
    }
    if (x.hasError()) throw std::runtime_error(lyrdb.string() + ": " + x.errorString().toStdString());
    if (!sawRoot) throw std::runtime_error(lyrdb.string() + " is empty");
    QJsonArray list;
    int total = 0;
    for (const auto& [rule, r] : rules) {
        total += r.count;
        list.append(QJsonObject{{"rule", rule}, {"description", descriptions[rule]}, {"count", r.count}, {"markers", r.markers}});
    }
    return {{"total", total}, {"rules", list}};
}

}  // namespace

// LVS's cross-reference, read by KLayout's own API from its database: every circuit, net, device, pin and subcircuit paired
// layout to schematic with its status, a net's connections on each side, and where each item that differs lies in the layout.
const char* kXrefScript = R"PY(
import json, pya
lvs = pya.LayoutVsSchematic()
lvs.read(lvsdb)
x, ly = lvs.xref(), lvs.internal_layout()
dbu = ly.dbu
status = {x.Match: "match", x.MatchWithWarning: "warning", x.Mismatch: "mismatch", x.NoMatch: "nomatch", x.Skipped: "skipped", x.None_: "none"}
top = lvs.internal_top_cell().name
def name(o): return o.expanded_name() if o else None
def um(b): return [round(b.left * dbu, 4), round(b.bottom * dbu, 4), round(b.right * dbu, 4), round(b.top * dbu, 4)]
def term(t): return "%s.%s" % (t.device().expanded_name(), t.terminal_def().name) if t else None
def scpin(t): return "%s.%s" % (t.subcircuit().expanded_name(), t.pin().name()) if t else None
circuits = []
for cp in x.each_circuit_pair():
    a, b = cp.first(), cp.second()
    items, counts = [], {}
    def add(kind, pair, extra):
        st = status[pair.status()]
        counts.setdefault(kind, {}).setdefault(st, 0)
        counts[kind][st] += 1
        it = {"kind": kind, "layout": name(pair.first()), "schematic": name(pair.second()), "status": st}
        if st != "match":
            it.update(extra(pair))
        items.append(it)
    def net(n):
        out = {"connections": [[term(t.first()), term(t.second())] for t in x.each_net_terminal_pair(n)] +
                              [[scpin(t.first()), scpin(t.second())] for t in x.each_net_subcircuit_pin_pair(n)],
               "pins": [[p.first() and p.first().pin().name(), p.second() and p.second().pin().name()] for p in x.each_net_pin_pair(n)]}
        if n.first():
            box, shapes = pya.Box(), []
            for i in lvs.layer_indexes():  # merged layer by layer: across them, a well would swallow the wires in it
                region = lvs.shapes_of_net(n.first(), lvs.layer_by_index(i), True).merged()
                box += region.bbox()
                shapes += [um(p.bbox()) for p in region.each()]
            if not box.empty():
                out["box"] = um(box)
                out["shapes"] = shapes[:200]  # ponytail: a chip-wide net is cut at 200 boxes; its box is still whole
        return out
    def device(d):
        out = {}
        f, g = d.first(), d.second()
        if f:
            out["class"] = f.device_class().name
            if f.device_abstract:
                bb = ly.cell(f.device_abstract.cell_index()).dbbox().transformed(f.trans)
                out["box"] = [round(bb.left, 4), round(bb.bottom, 4), round(bb.right, 4), round(bb.top, 4)]
        elif g:
            out["class"] = g.device_class().name
        params = {}
        for side, dev in ((0, f), (1, g)):
            if dev:
                for p in dev.device_class().parameter_definitions():
                    params.setdefault(p.name, [None, None])[side] = dev.parameter(p.name)
        out["parameters"] = params
        return out
    def subcircuit(sc):
        out = {}
        f = sc.first()
        ci = f and f.circuit_ref() and f.circuit_ref().cell_index
        ci = ci() if callable(ci) else ci
        if ci is not None and ci is not False:
            bb = ly.cell(ci).dbbox().transformed(f.trans)
            out["box"] = [round(bb.left, 4), round(bb.bottom, 4), round(bb.right, 4), round(bb.top, 4)]
            out["cell"] = f.circuit_ref().name
        return out
    if a and b:
        for n in x.each_net_pair(cp): add("net", n, net)
        for d in x.each_device_pair(cp): add("device", d, device)
        for p in x.each_pin_pair(cp): add("pin", p, lambda p: {})
        for sc in x.each_subcircuit_pair(cp): add("subcircuit", sc, subcircuit)
    circuits.append({"layout": a.name if a else None, "schematic": b.name if b else None, "status": status[cp.status()],
                     "top": bool(a and a.name == top), "counts": counts, "items": items})
json.dump({"top": top, "circuits": circuits}, open(out, "w"))
)PY";

// A script of ours run by KLayout: it reads what defs name and writes JSON to `out`
QJsonObject klayoutJson(const char* text, const fs::path& script, const fs::path& out, const std::vector<std::string>& defs) {
    const RtlFlowTools tools = RtlFlowTools::detect();
    if (tools.klayout.empty()) throw std::runtime_error("KLayout reads LVS's report: " + tools.missing());
    std::ofstream(script) << text;
    const fs::path tmp = out.string() + ".tmp";
    std::vector<std::string> args{"-b", "-rd", "out=" + tmp.string()};
    for (const auto& d : defs) {
        args.push_back("-rd");
        args.push_back(d);
    }
    args.push_back("-r");
    args.push_back(script.string());
    acutesim::orchestration::Subprocess p(tools.klayout, args);
    const auto r = p.run();
    if (r.exit_code != 0 || !fs::exists(tmp)) throw std::runtime_error("KLayout could not read the netlists: " + r.stdout_output + r.stderr_output);
    fs::rename(tmp, out);
    return readJson(out);
}

// A cached result stands while its source has not changed and the script that made it is still this one
bool fresh(const fs::path& cache, const fs::path& source, const fs::path& script, const char* text) {
    if (!fs::exists(cache) || fs::last_write_time(cache) < fs::last_write_time(source)) return false;
    std::ifstream f(script);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()) == text;
}

// Cached beside the database
QJsonObject lvsCrossReference(const fs::path& lvsdb) {
    if (!fs::exists(lvsdb)) throw std::invalid_argument("no LVS report: " + lvsdb.string());
    const fs::path cache = lvsdb.string() + ".xref.json", script = lvsdb.string() + ".xref.py";
    if (fresh(cache, lvsdb, script, kXrefScript)) return readJson(cache);
    return klayoutJson(kXrefScript, script, cache, {"lvsdb=" + lvsdb.string()});
}

// The electrical rule check: what no comparison of two netlists can see, because both may have it. Run over each netlist
// of an LVS report, flat (or over a schematic's CDL alone): device terminals by role (a path through the device, a gate, a bulk,
// a plate), supplies by name.
const char* kErcScript = R"PY(
import json, re, pya
sides, ly, pins = [], None, {"layout": set(), "schematic": set()}
if lvsdb:
    lvs = pya.LayoutVsSchematic()
    lvs.read(lvsdb)
    ly = lvs.internal_layout()
    sides = [("layout", lvs.netlist()), ("schematic", lvs.reference)]
    x, top = lvs.xref(), lvs.internal_top_cell().name
    for cp in x.each_circuit_pair():  # a top-level net that is a pin on one side is one on the other: LVS paired them
        if cp.first() and cp.second() and cp.first().name == top:
            for n in x.each_net_pair(cp):
                if n.first() and n.second() and (n.first().pin_count() or n.second().pin_count()):
                    pins["layout"].add(n.first().expanded_name())
                    pins["schematic"].add(n.second().expanded_name())
else:
    nl = pya.Netlist()
    nl.read(cdl, pya.NetlistSpiceReader())
    sides = [("schematic", nl)]
given = {"power": [n.lower() for n in power.split(",") if n], "ground": [n.lower() for n in ground.split(",") if n]}
usual = {"power": re.compile(r"^(a|d|io)?(vdd|vcc|vpwr)", re.I), "ground": re.compile(r"^((a|d|io)?(vss|gnd|vgnd)|0$)", re.I)}
def supply(kind, name): return name.lower() in given[kind] if given[kind] else bool(usual[kind].match(name))
def roles(dc):
    names = {t.name for t in dc.terminal_definitions()}
    if {"S", "G", "D"} <= names: return {"S": "path", "D": "path", "G": "gate", "B": "bulk"}
    if {"C", "B", "E"} <= names: return {"C": "path", "E": "path", "B": "gate", "S": "bulk"}
    if isinstance(dc, (pya.DeviceClassCapacitor, pya.DeviceClassDiode)): return {"A": "plate", "B": "plate", "C": "plate", "W": "bulk"}
    if isinstance(dc, (pya.DeviceClassResistor, pya.DeviceClassInductor)): return {"A": "path", "B": "path", "W": "bulk"}
    return {n: "path" for n in names}  # a class this does not know: nothing is claimed about it
def polarity(dc):  # ponytail: by the class's name; a PDK whose names say neither gets no bulk-supply check
    n = dc.name.lower()
    return "p" if re.search(r"pmos|pfet|pch", n) else "n" if re.search(r"nmos|nfet|nch", n) else None
result = {"sides": {}, "violations": []}
for side, netlist in sides:
    nl = netlist.dup()
    nl.flatten()  # ponytail: the whole chip flat in memory; past some millions of transistors, check cell by cell instead
    counts = {"devices": 0, "nets": 0, "power": [], "ground": []}
    def box(d):
        if side != "layout" or not d.device_abstract: return None
        b = ly.cell(d.device_abstract.cell_index()).dbbox().transformed(d.trans)
        return [round(b.left, 4), round(b.bottom, 4), round(b.right, 4), round(b.top, 4)]
    def add(rule, net, devices, detail):
        boxes = [b for b in (box(d) for d in devices[:50]) if b]  # ponytail: a net's first 50 devices are marked
        result["violations"].append({"rule": rule, "side": side, "net": net and net.expanded_name(), "devices": [d.expanded_name() for d in devices[:50]],
                                  "device_count": len(devices), "detail": detail, "boxes": boxes})
    for c in nl.each_circuit():
        nets = list(c.each_net())
        counts["devices"] += len(list(c.each_device()))
        counts["nets"] += len(nets)
        key = lambda n: (n.expanded_name(), n.cluster_id)  # two pieces of an open net may share a name
        is_pin = lambda n: n.pin_count() > 0 or n.expanded_name() in pins[side]
        kinds = {key(n): ("power" if supply("power", n.expanded_name()) else "ground" if supply("ground", n.expanded_name()) else None) for n in nets}
        for k in ("power", "ground"): counts[k] += sorted({n[0] for n, v in kinds.items() if v == k})
        has_mos = False
        parent = {}
        def find(a):
            while parent.setdefault(a, a) != a:
                parent[a] = parent[parent[a]]
                a = parent[a]
            return a
        reaches = {}  # a net's root -> what its paths reach: a supply, a pin
        by_role = {}
        for n in nets:
            r = {}
            for t in n.each_terminal():
                role = roles(t.device().device_class()).get(t.terminal_def().name, "path")
                r.setdefault(role, []).append(t.device())
            by_role[key(n)] = r
        for d in c.each_device():
            dc = d.device_class()
            rl = roles(dc)
            ends = [d.net_for_terminal(t.id()) for t in dc.terminal_definitions() if rl.get(t.name) == "path"]
            ends = [key(n) for n in ends if n]
            for a in ends:
                for b in ends:
                    if kinds[a] is None and kinds[b] is None: parent[find(a)] = find(b)
            pol = polarity(dc) if "G" in rl else None
            has_mos = has_mos or "G" in rl
            bulk = d.net_for_terminal("B") if pol else None
            if bulk and kinds[key(bulk)] == ("power" if pol == "n" else "ground"):
                add("bulk_supply", bulk, [d], "the bulk of %s %s is on %s %s: its junctions conduct" % (dc.name, d.expanded_name(), "power" if pol == "n" else "ground", bulk.expanded_name()))
        for d in c.each_device():
            dc = d.device_class()
            rl = roles(dc)
            ends = [d.net_for_terminal(t.id()) for t in dc.terminal_definitions() if rl.get(t.name) == "path"]
            ends = [key(n) for n in ends if n]
            for a in ends:
                if kinds[a] is None:
                    reaches.setdefault(find(a), set()).update("supply" for b in ends if kinds[b])
        for n in nets:
            if kinds[key(n)] is None and is_pin(n) and key(n) in parent: reaches.setdefault(find(key(n)), set()).add("pin")
        if has_mos:
            for k in ("power", "ground"):
                if not any(v == k for v in kinds.values()): add("supply_missing", None, [], "no net is named as %s" % k)
        islands = {}
        for n in nets:
            name, r = key(n), by_role[key(n)]
            if kinds[name] or is_pin(n): continue
            total = sum(len(v) for v in r.values()) + n.subcircuit_pin_count()
            if "gate" in r and "path" not in r:
                add("floating_gate", n, r["gate"], "it joins only the gates of " + ", ".join(d.expanded_name() for d in r["gate"][:8]))
            elif "bulk" in r and len(r) == 1:
                add("floating_bulk", n, r["bulk"], "it joins only the bulks of " + ", ".join(d.expanded_name() for d in r["bulk"][:8]))
            elif total == 1:
                d = (r.get("path") or r.get("plate"))[0]
                add("dangling", n, [d], "it joins one terminal only, of " + d.expanded_name())
            elif "path" in r and not reaches.get(find(name)):
                islands.setdefault(find(name), []).append(n)
        for root, members in islands.items():
            devices = []
            for n in members: devices += by_role[key(n)]["path"]
            devices = list({d.expanded_name(): d for d in devices}.values())
            add("no_supply_path", members[0], devices, "no channel or resistor leads from %s to a supply or a pin" % ", ".join(n.expanded_name() for n in members[:8]))
    result["sides"][side] = counts
json.dump(result, open(out, "w"))
)PY";

const std::map<QString, QString> kErcRules = {
    {"floating_gate", "a gate that nothing drives"},
    {"floating_bulk", "a well or substrate with no tap"},
    {"bulk_supply", "a bulk on the wrong supply"},
    {"dangling", "a net that joins one terminal"},
    {"no_supply_path", "nodes with no path to a supply or a pin"},
    {"supply_missing", "no power or no ground net"}};

std::string supplyNames(const QJsonObject& a, const char* key) {
    QStringList names;
    for (const auto& v : a[key].toArray()) {
        if (v.toString().isEmpty() || v.toString().contains(',')) throw std::invalid_argument(std::string(key) + ": '" + v.toString().toStdString() + "' is no net name");
        names << v.toString();
    }
    return names.join(',').toStdString();
}

// {clean, total, sides: {layout, schematic: {devices, nets, power, ground}}, rules: [{rule, description, count, violations}]}.
// Of an LVS report (cached beside it while the supplies are the usual names), or of a CDL file.
QJsonObject ercCheck(const fs::path& lvsdb, const fs::path& cdl, const QJsonObject& a = {}) {
    const fs::path source = lvsdb.empty() ? cdl : lvsdb;
    if (!fs::exists(source)) throw std::invalid_argument("no netlist to check: " + source.string());
    const bool usual = !a.contains("power") && !a.contains("ground");
    const fs::path cache = source.string() + (usual ? ".erc.json" : ".erc.named.json"), script = source.string() + ".erc.py";
    const QJsonObject raw = usual && fresh(cache, source, script, kErcScript)
                                ? readJson(cache)
                                : klayoutJson(kErcScript, script, cache,
                                              {"lvsdb=" + lvsdb.string(), "cdl=" + cdl.string(), "power=" + supplyNames(a, "power"), "ground=" + supplyNames(a, "ground")});
    std::map<QString, QJsonArray> byRule;
    for (const auto& v : raw["violations"].toArray()) byRule[v.toObject()["rule"].toString()].append(v);
    QJsonArray rules;
    for (const auto& [rule, list] : byRule) {
        const auto d = kErcRules.find(rule);
        if (d == kErcRules.end()) throw std::runtime_error("the check reported an unknown rule: " + rule.toStdString());
        rules.append(QJsonObject{{"rule", rule}, {"description", d->second}, {"count", list.size()}, {"violations", list}});
    }
    const int total = raw["violations"].toArray().size();
    return {{"clean", total == 0}, {"total", total}, {"sides", raw["sides"]}, {"rules", rules}};
}

int ercViolations(const std::string& lvsdb) { return ercCheck(lvsdb, {})["total"].toInt(); }

// An electrical check's violations as markers: a layout one at each device it names, a schematic one by name only
QJsonArray ercMarkers(const QJsonObject& erc) {
    QJsonArray rules;
    for (const auto& rv : erc["rules"].toArray()) {
        QJsonArray markers;
        for (const auto& vv : rv.toObject()["violations"].toArray()) {
            const QJsonObject v = vv.toObject();
            const QString net = v["net"].toString();
            QJsonObject marker{{"cell", QString()}, {"label", (net.isEmpty() ? QString() : net + ", ") + v["side"].toString()}, {"description", v["detail"]},
                               {"schematic_net", net.isEmpty() || net.startsWith('$') ? QJsonValue() : QJsonValue(net)}, {"points", QJsonArray{}}};
            if (v["boxes"].toArray().isEmpty()) markers.append(marker);
            for (const auto& bv : v["boxes"].toArray()) {
                const QJsonArray b = bv.toArray();
                marker["points"] = QJsonArray{QJsonArray{b[0], b[1]}, QJsonArray{b[2], b[3]}};
                markers.append(marker);
            }
        }
        rules.append(QJsonObject{{"rule", "ERC: " + rv.toObject()["description"].toString()}, {"count", markers.size()},
                                 {"description", "electrical rule " + rv.toObject()["rule"].toString()}, {"markers", markers}});
    }
    return rules;
}

// The LVS report of a verification run or an implementation run
fs::path lvsdbOf(Context& c, const QJsonObject& a) {
    if (a.contains("run") == a.contains("flow_run")) throw std::invalid_argument("give run, or flow_run");
    if (a.contains("run")) {
        if (!a["run"].toString().contains("/lvs.")) throw std::invalid_argument("not an LVS run: " + s(a, "run"));
        return runPath(c, a) / "report.lvsdb";
    }
    static const QRegularExpression name("^[A-Za-z0-9_]+/run\\.[0-9]+$");
    if (!name.match(a["flow_run"].toString()).hasMatch()) throw std::invalid_argument("an implementation run is named <design>/run.N");
    const fs::path dir = base(c) / "implementation" / s(a, "flow_run");
    const QJsonObject rec = readJson(dir / "run.json");
    return dir / "results" / s(rec, "platform") / s(rec, "design") / "base" / "6_lvs.lvsdb";
}

// What differs, as markers the layout viewer walks: one group per kind of difference, each marker named
QJsonObject lvsMarkers(const QJsonObject& xref) {
    std::map<QString, QJsonArray> groups;
    int total = 0;
    for (const auto& cv : xref["circuits"].toArray()) {
        const QJsonObject circuit = cv.toObject();
        for (const auto& iv : circuit["items"].toArray()) {
            const QJsonObject it = iv.toObject();
            if (it["status"] == "match" || it["status"] == "warning") continue;
            const QString kind = it["kind"].toString();
            const QString group = it["schematic"].isNull() ? kind + "s only in the layout" : it["layout"].isNull() ? kind + "s only in the schematic" : kind + "s that differ";
            QStringList joins;
            for (const auto& cn : it["connections"].toArray()) {
                const QJsonArray pair = cn.toArray();
                joins << (pair[0].isNull() ? "(none)" : pair[0].toString()) + " / " + (pair[1].isNull() ? "(none)" : pair[1].toString());
            }
            const QString label = (it["layout"].isNull() ? "(none)" : it["layout"].toString()) + " / " + (it["schematic"].isNull() ? "(none)" : it["schematic"].toString());
            QJsonObject marker{{"cell", circuit["top"].toBool() ? QString() : circuit["layout"].toString()}, {"label", label}, {"schematic_net", kind == "net" ? it["schematic"] : QJsonValue()},
                               {"description", joins.isEmpty() ? QString("layout / schematic") : "joins, layout / schematic: " + joins.join(", ")}};
            if (it.contains("box")) {
                const QJsonArray b = it["box"].toArray();
                marker["points"] = QJsonArray{QJsonArray{b[0], b[1]}, QJsonArray{b[2], b[3]}};
            } else {
                marker["points"] = QJsonArray{};
            }
            groups[group].append(marker);
            ++total;
        }
    }
    QJsonArray rules;
    for (const auto& [group, markers] : groups)
        rules.append(QJsonObject{{"rule", group}, {"count", markers.size()}, {"description", "LVS: " + group + ", named layout / schematic"}, {"markers", markers}});
    return {{"total", total}, {"rules", rules}};
}

void addVerifyCommands(Registry& r) {
    r.add({"verify_drc", "Runs the process's DRC deck over a layout view; returns the run with its violation count (verify_markers lists them). Density rules are off by default: "
                         "they judge a whole chip with its fill. Waits, or with wait false starts it in the background (poll verify_status).",
           object({{"library", kLib}, {"cell", kCell}}, {{"view", kView}, {"density", param("boolean", "apply the deck's density rules")}, {"wait", param("boolean", "wait for it (default true)")}}),
           [](Context& c, const QJsonObject& a) {
               oadb::Library& l = c.workspace.library(s(a, "library"));
               const flow::Platform& pdk = platformOf(l);
               const RtlFlowTools tools = RtlFlowTools::detect();
               if (tools.klayout.empty() || tools.flowDir.empty()) throw std::runtime_error("KLayout and the platform's decks are needed: " + tools.missing());
               std::string run;
               const fs::path dir = newRun(c, s(a, "library"), s(a, "cell"), "drc", &run);
               exportGds(c.workspace, s(a, "library"), s(a, "cell"), viewOf(a), (dir / "layout.gds").string());
               QJsonObject rec{{"run", q(run)}, {"kind", "drc"}, {"library", a["library"]}, {"cell", a["cell"]}, {"view", q(viewOf(a))}, {"state", "running"}, {"started", now()}};
               writeRecord(dir, rec);
               std::vector<std::string> args{"-b", "-rd", "in_gds=" + (dir / "layout.gds").string(), "-rd", "report_file=" + (dir / "report.lyrdb").string()};
               if (!a["density"].toBool(false)) { args.push_back("-rd"); args.push_back("densityRules=false"); }
               for (const auto& d : pdk.drcDefines)
                   if (d.rfind("densityRules", 0) != 0) { args.push_back("-rd"); args.push_back(d); }
               args.push_back("-r");
               args.push_back(flow::platformFile(pdk, tools.flowDir, pdk.drcDeck));
               const auto execute = [dir, rec, args, tools](const std::atomic<bool>* cancel) {
                   QJsonObject done = rec;
                   std::string why;
                   const bool ran = runDeck(tools, dir, args, cancel, &why);
                   done["finished"] = now();
                   done["state"] = cancel && *cancel ? "stopped" : "done";
                   if (!ran) {
                       done["error"] = q(why);
                   } else {
                       try {
                           const QJsonObject markers = readMarkers(dir / "report.lyrdb");
                           done["violations"] = markers["total"];
                           done["clean"] = markers["total"].toInt() == 0;
                       } catch (const std::exception& e) {
                           done["error"] = q(e.what());
                       }
                   }
                   writeRecord(dir, done);
               };
               if (!a.contains("wait") || a["wait"].toBool()) {
                   execute(nullptr);
                   return QJsonValue(readJson(dir / "run.json"));
               }
               jobs(c).start(dir.string(), [execute](const std::atomic<bool>& cancel) { execute(&cancel); });
               return QJsonValue(rec);
           },
           true});
    r.add({"verify_lvs", "Compares a layout view with its schematic through the process's LVS deck; returns whether they match, with device and net counts on each side, "
                         "and how many electrical rule violations the two netlists have (verify_erc lists them). Waits, or with wait false starts it in the background.",
           object({{"library", kLib}, {"cell", kCell}}, {{"view", kView}, {"schematic", param("string", "the schematic view (default \"schematic\")")}, {"wait", param("boolean", "wait for it (default true)")}}),
           [](Context& c, const QJsonObject& a) {
               oadb::Library& l = c.workspace.library(s(a, "library"));
               const flow::Platform& pdk = platformOf(l);
               if (pdk.lvsDeck.empty()) throw std::invalid_argument(pdk.noLvs);
               const RtlFlowTools tools = RtlFlowTools::detect();
               if (tools.klayout.empty() || tools.flowDir.empty()) throw std::runtime_error("KLayout and the platform's decks are needed: " + tools.missing());
               const std::string cdl = schematicCdl(c.workspace, s(a, "library"), s(a, "cell"), a.contains("schematic") ? s(a, "schematic") : "schematic");
               std::string run;
               const fs::path dir = newRun(c, s(a, "library"), s(a, "cell"), "lvs", &run);
               exportGds(c.workspace, s(a, "library"), s(a, "cell"), viewOf(a), (dir / "layout.gds").string());
               std::ofstream(dir / "schematic.cdl") << cdl;
               // The deck's includes are relative to where it lies; a copy here keeps them findable
               const fs::path deckFile = flow::platformFile(pdk, tools.flowDir, pdk.lvsDeck);
               {
                   std::ifstream in(deckFile);
                   std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                   const std::string include = "# %include rule_decks/";
                   for (size_t at = 0; (at = text.find(include, at)) != std::string::npos;) {
                       const std::string to = "# %include " + (deckFile.parent_path() / "rule_decks").string() + "/";
                       text.replace(at, include.size(), to);
                       at += to.size();
                   }
                   std::ofstream(dir / "deck.lvs") << text;
               }
               QJsonObject rec{{"run", q(run)}, {"kind", "lvs"}, {"library", a["library"]}, {"cell", a["cell"]}, {"view", q(viewOf(a))},
                               {"schematic", a.contains("schematic") ? a["schematic"] : QJsonValue("schematic")}, {"state", "running"}, {"started", now()}};
               writeRecord(dir, rec);
               const std::vector<std::string> args{"-b", "-rd", "input=" + (dir / "layout.gds").string(), "-rd", "schematic=" + (dir / "schematic.cdl").string(), "-rd", "topcell=" + s(a, "cell"),
                                                   "-rd", "report=" + (dir / "report.lvsdb").string(), "-rd", "target_netlist=" + (dir / "extracted.cir").string(), "-rd", "run_mode=deep",
                                                   "-r", (dir / "deck.lvs").string()};
               const auto execute = [dir, rec, args, tools](const std::atomic<bool>* cancel) {
                   QJsonObject done = rec;
                   std::string why;
                   const bool ran = runDeck(tools, dir, args, cancel, &why);
                   done["finished"] = now();
                   done["state"] = cancel && *cancel ? "stopped" : "done";
                   std::ifstream logFile(dir / "deck.log");
                   const std::string log((std::istreambuf_iterator<char>(logFile)), std::istreambuf_iterator<char>());
                   if (!ran && log.find("Netlists") == std::string::npos) {
                       done["error"] = q(why);
                   } else {
                       const LVSResult lvs = readLvsReport((dir / "report.lvsdb").string());
                       if (!lvs.success) {
                           done["error"] = lvs.errors.empty() ? QString("its report could not be read") : q(lvs.errors.front());
                       } else {
                           done["matched"] = lvs.matched;
                           done["clean"] = lvs.matched;
                           done["mismatches"] = lvs.mismatchCount;
                           done["devices_layout"] = lvs.deviceCountLayout;
                           done["devices_schematic"] = lvs.deviceCountSchematic;
                           done["nets_layout"] = lvs.netCountLayout;
                           done["nets_schematic"] = lvs.netCountSchematic;
                           try {
                               done["erc_violations"] = ercViolations((dir / "report.lvsdb").string());
                           } catch (const std::exception& e) {
                               done["erc_error"] = q(e.what());
                           }
                       }
                   }
                   writeRecord(dir, done);
               };
               if (!a.contains("wait") || a["wait"].toBool()) {
                   execute(nullptr);
                   return QJsonValue(readJson(dir / "run.json"));
               }
               jobs(c).start(dir.string(), [execute](const std::atomic<bool>& cancel) { execute(&cancel); });
               return QJsonValue(rec);
           },
           true});
    r.add({"verify_status", "A verification run's state and findings, with its log's tail.", object({{"run", kRun}}),
           [](Context& c, const QJsonObject& a) {
               const fs::path dir = runPath(c, a);
               QJsonObject rec = readJson(dir / "run.json");
               std::ifstream f(dir / "deck.log");
               if (f) {
                   const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                   rec["log_tail"] = q(text.size() > 32768 ? text.substr(text.size() - 32768) : text);
               }
               return QJsonValue(rec);
           },
           true});
    r.add({"verify_list", "The verification runs on disk, per layout oldest first.", object({}),
           [](Context& c, const QJsonObject&) {
               QJsonArray out;
               const fs::path root = base(c) / "verification";
               if (!fs::is_directory(root)) return QJsonValue(out);
               std::vector<std::string> names;
               for (const auto& t : fs::directory_iterator(root)) {
                   if (!t.is_directory()) continue;
                   for (const auto& e : fs::directory_iterator(t.path()))
                       if (e.is_directory()) names.push_back(t.path().filename().string() + "/" + e.path().filename().string());
               }
               std::sort(names.begin(), names.end());
               for (const auto& name : names) {
                   try {
                       out.append(readJson(root / name / "run.json"));
                   } catch (const std::exception& e) {
                       out.append(QJsonObject{{"run", q(name)}, {"error", q(e.what())}});
                   }
               }
               return QJsonValue(out);
           },
           true});
    r.add({"verify_lvs_report", "LVS's cross-reference, read from its report by KLayout: each circuit layout to schematic with its status and counts, and its nets, devices, "
                                "pins and subcircuits, each named on both sides with its status (match, warning, mismatch, nomatch). One that differs says what it joins on "
                                "each side (device.terminal, subcircuit.pin), its parameters on each side, and where it lies in the layout, in microns in its circuit's cell. "
                                "Only what differs, unless all.",
           object({}, {{"run", kRun}, {"flow_run", param("string", "an implementation run: <design>/run.N")}, {"all", param("boolean", "every item, matched ones too")}}),
           [](Context& c, const QJsonObject& a) {
               QJsonObject xref = lvsCrossReference(lvsdbOf(c, a));
               bool matched = true;
               QJsonArray circuits;
               for (const auto& cv : xref["circuits"].toArray()) {
                   QJsonObject circuit = cv.toObject();
                   matched = matched && (circuit["status"] == "match" || circuit["status"] == "warning");
                   if (!a["all"].toBool()) {
                       QJsonArray differ;
                       for (const auto& iv : circuit["items"].toArray())
                           if (iv.toObject()["status"] != "match") differ.append(iv);
                       circuit["items"] = differ;
                   }
                   circuits.append(circuit);
               }
               xref["circuits"] = circuits;
               xref["matched"] = matched;
               return QJsonValue(xref);
           },
           true});
    r.add({"verify_erc", "The electrical rule check: gates that nothing drives, wells with no tap, bulks on the wrong supply, nets that join one terminal, nodes with "
                         "no path to a supply or a pin, a missing supply. Of the layout's and the schematic's netlists in an LVS run (run) or an implementation run "
                         "(flow_run), each flat, or of a schematic alone (library, cell). Supplies are nets named like vdd, vcc, vpwr and vss, gnd, unless power and "
                         "ground name them. Returns rules with their violations: the side, the net, the devices, why, and each device's box in the layout in microns.",
           object({}, {{"run", kRun}, {"flow_run", param("string", "an implementation run: <design>/run.N")}, {"library", kLib}, {"cell", kCell},
                       {"schematic", param("string", "with library and cell: the schematic view (default \"schematic\")")},
                       {"power", QJsonObject{{"type", "array"}, {"description", "the power nets' names"}, {"items", QJsonObject{{"type", "string"}}}}},
                       {"ground", QJsonObject{{"type", "array"}, {"description", "the ground nets' names"}, {"items", QJsonObject{{"type", "string"}}}}}}),
           [](Context& c, const QJsonObject& a) {
               if (a.contains("library") != a.contains("cell")) throw std::invalid_argument("library and cell go together");
               if (a.contains("run") + a.contains("flow_run") + a.contains("library") != 1) throw std::invalid_argument("give run, flow_run, or library and cell");
               if (!a.contains("library")) {
                   if (a.contains("schematic")) throw std::invalid_argument("schematic goes with library and cell");
                   QJsonObject which;
                   for (const char* k : {"run", "flow_run"})
                       if (a.contains(k)) which[k] = a[k];
                   return QJsonValue(ercCheck(lvsdbOf(c, which), {}, a));
               }
               QTemporaryDir tmp;
               if (!tmp.isValid()) throw std::runtime_error("no temporary directory");
               const fs::path cdl = fs::path(tmp.path().toStdString()) / "schematic.cdl";
               std::ofstream(cdl) << schematicCdl(c.workspace, s(a, "library"), s(a, "cell"), a.contains("schematic") ? s(a, "schematic") : "schematic");
               return QJsonValue(ercCheck({}, cdl, a));
           },
           true});
    r.add({"verify_markers", "What a check found, as markers: a DRC's rule by rule, or an LVS's differences kind by kind (each named layout / schematic) and then its "
                             "electrical rule violations (rules named ERC: ...). run, or flow_run with check drc (default) or lvs for an implementation run's. Each marker "
                             "gives its cell and points, in microns in that cell.",
           object({}, {{"run", kRun}, {"flow_run", param("string", "an implementation run: <design>/run.N")},
                       {"check", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"drc", "lvs"}}, {"description", "for flow_run: which check's (default drc)"}}}}),
           [](Context& c, const QJsonObject& a) {
               if (a.contains("run") == a.contains("flow_run")) throw std::invalid_argument("give run, or flow_run");
               if (a.contains("check") && a.contains("run")) throw std::invalid_argument("check is for flow_run: a verification run is one check");
               if ((a.contains("run") && a["run"].toString().contains("/lvs.")) || a["check"] == "lvs") {
                   QJsonObject which = a;
                   which.remove("check");
                   const fs::path lvsdb = lvsdbOf(c, which);
                   QJsonObject markers = lvsMarkers(lvsCrossReference(lvsdb));
                   QJsonArray rules = markers["rules"].toArray();
                   int total = markers["total"].toInt();
                   for (const auto& rule : ercMarkers(ercCheck(lvsdb, {}))) {
                       rules.append(rule);
                       total += rule.toObject()["count"].toInt();
                   }
                   markers["rules"] = rules;
                   markers["total"] = total;
                   if (a.contains("run")) markers["schematic_view"] = readJson(runPath(c, a) / "run.json")["schematic"].toString("schematic");
                   return QJsonValue(markers);
               }
               if (a.contains("run")) {
                   const fs::path dir = runPath(c, a);
                   if (!fs::exists(dir / "report.lyrdb")) throw std::invalid_argument("this run has no DRC report");
                   return QJsonValue(readMarkers(dir / "report.lyrdb"));
               }
               static const QRegularExpression name("^[A-Za-z0-9_]+/run\\.[0-9]+$");
               if (!name.match(a["flow_run"].toString()).hasMatch()) throw std::invalid_argument("an implementation run is named <design>/run.N");
               const fs::path dir = base(c) / "implementation" / s(a, "flow_run");
               const QJsonObject rec = readJson(dir / "run.json");
               return QJsonValue(readMarkers(dir / "reports" / s(rec, "platform") / s(rec, "design") / "base" / "6_drc.lyrdb"));
           },
           true});
}

}  // namespace cmd
