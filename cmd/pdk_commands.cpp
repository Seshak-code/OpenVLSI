// PDK setup: analogLib (ideal sources and passives) and a PDK's device library, as primitives with symbols, spice
// templates, parameter defaults and the model files they need (sim/netlist.h)
#include "cmd/registry.h"
#include "oadb/oadb.h"

#include <QJsonDocument>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace cmd {

namespace {

namespace fs = std::filesystem;
using oadb::Point;

struct Cell {
    std::string name, prefix, tmpl, instPrefix;
    std::vector<oadb::Pin> pins;
    std::vector<oadb::Shape> shapes;
    QJsonObject params;
    QJsonArray models;
};

oadb::Shape line(std::vector<Point> p, const char* layer = "device") { return {0, "line", layer, std::move(p), "", ""}; }
oadb::Shape label(Point at, const char* text, const char* justify = "lowerLeft") { return {0, "label", "annotate", {at}, text, justify}; }
oadb::Shape ellipse(Point a, Point b) { return {0, "ellipse", "device", {a, b}, "", ""}; }
oadb::Pin pin(const char* name, const char* dir, Point at) { return {0, name, dir, at}; }

// two terminals, PLUS on top and MINUS below, leads to a body drawn between y = 50 and -50
Cell twoTerminal(const char* name, const char* prefix, const char* tmpl, QJsonObject params, std::vector<oadb::Shape> body,
                 const char* value) {
    Cell c{name, prefix, tmpl, prefix, {pin("PLUS", "inputOutput", {0, 100}), pin("MINUS", "inputOutput", {0, -100})}, std::move(body),
           std::move(params), {}};
    c.shapes.push_back(line({{0, 100}, {0, 50}}, "pin"));
    c.shapes.push_back(line({{0, -50}, {0, -100}}, "pin"));
    c.shapes.push_back(label({50, 15}, "[@instanceName]"));
    if (*value) c.shapes.push_back(label({50, -15}, value, "upperLeft"));  // a cell with no value (iprobe) has no value label
    return c;
}
std::vector<oadb::Shape> source(std::vector<oadb::Shape> mark) {
    mark.push_back(ellipse({-40, -50}, {40, 50}));
    return mark;
}

std::vector<Cell> analogLib() {
    const QJsonObject none;
    return {
        twoTerminal("res", "R", "[|PLUS] [|MINUS] [@r]", {{"r", "1k"}},
                    {line({{0, 50}, {-25, 40}, {25, 25}, {-25, 10}, {25, -5}, {-25, -20}, {25, -35}, {0, -50}})}, "[@r]"),
        twoTerminal("cap", "C", "[|PLUS] [|MINUS] [@c]", {{"c", "1p"}},
                    {line({{-40, 10}, {40, 10}}), line({{-40, -10}, {40, -10}}), line({{0, 50}, {0, 10}}), line({{0, -10}, {0, -50}})}, "[@c]"),
        twoTerminal("ind", "L", "[|PLUS] [|MINUS] [@l]", {{"l", "1n"}},
                    {ellipse({-15, 25}, {15, 50}), ellipse({-15, 0}, {15, 25}), ellipse({-15, -25}, {15, 0}), ellipse({-15, -50}, {15, -25})},
                    "[@l]"),
        twoTerminal("vdc", "V", "[|PLUS] [|MINUS] dc [@vdc] [@acm:ac %]", {{"vdc", "0"}},
                    source({line({{-10, 25}, {10, 25}}), line({{0, 15}, {0, 35}}), line({{-10, -25}, {10, -25}})}), "[@vdc] [@acm]"),
        // a 0 V source in a loop, + toward the loop's input: stb's probe (its element name: the instance's, V-prefixed)
        twoTerminal("iprobe", "V", "[|PLUS] [|MINUS] dc 0", {}, {line({{0, 50}, {0, -50}}), line({{-10, 10}, {0, -5}, {10, 10}})}, ""),
        twoTerminal("idc", "I", "[|PLUS] [|MINUS] dc [@idc] [@acm:ac %]", {{"idc", "0"}},
                    source({line({{0, -30}, {0, 30}}), line({{-10, 15}, {0, 30}, {10, 15}})}), "[@idc] [@acm]"),
        twoTerminal("vpulse", "V", "[|PLUS] [|MINUS] dc [@v1] [@acm:ac %] pulse([@v1] [@v2] [@td] [@tr] [@tf] [@pw] [@per])",
                    {{"v1", "0"}, {"v2", "1"}, {"td", "0"}, {"tr", "1n"}, {"tf", "1n"}, {"pw", "5n"}, {"per", "10n"}},
                    source({line({{-25, -15}, {-10, -15}, {-10, 15}, {10, 15}, {10, -15}, {25, -15}})}), "[@v1] [@v2] [@per]"),
        twoTerminal("vsin", "V", "[|PLUS] [|MINUS] dc [@vdc] [@acm:ac %] sin([@vdc] [@va] [@freq])", {{"vdc", "0"}, {"va", "1"}, {"freq", "1k"}},
                    source({line({{-25, 0}, {-15, 12}, {-5, 12}, {5, -12}, {15, -12}, {25, 0}})}), "[@va] [@freq]"),
        [] {
            Cell c{"vcvs", "E", "[|PLUS] [|MINUS] [|NCP] [|NCN] [@gain]", "E",
                   {pin("PLUS", "inputOutput", {0, 100}), pin("MINUS", "inputOutput", {0, -100}), pin("NCP", "input", {-100, 50}),
                    pin("NCN", "input", {-100, -50})},
                   {{0, "polygon", "device", {{0, 50}, {40, 0}, {0, -50}, {-40, 0}}, "", ""}, line({{0, 100}, {0, 50}}, "pin"),
                    line({{0, -50}, {0, -100}}, "pin"), line({{-100, 50}, {-60, 50}}, "pin"), line({{-100, -50}, {-60, -50}}, "pin"),
                    label({50, 15}, "[@instanceName]"), label({50, -15}, "[@gain]", "upperLeft")},
                   {{"gain", "1"}},
                   {}};
            return c;
        }(),
    };
}

// a MOS: D on top, S below, G left, B right; p adds the gate's bubble
Cell mos(const std::string& name, bool p, const std::string& model, QJsonArray models) {
    Cell c{name, "X", "[|D] [|G] [|S] [|B] " + model + " w=[@w] l=[@l] ng=[@ng] m=[@m] mm_ok={mc_mismatch}", "M",
           {pin("D", "inputOutput", {0, 100}), pin("G", "input", {-100, 0}), pin("S", "inputOutput", {0, -100}), pin("B", "inputOutput", {100, 0})},
           {line({{-40, -40}, {-40, 40}}), line({{-20, -50}, {-20, 50}}), line({{-20, 40}, {0, 40}, {0, 100}}, "pin"),
            line({{-20, -40}, {0, -40}, {0, -100}}, "pin"), line({{-20, 0}, {100, 0}}, "pin"), label({30, 50}, "[@instanceName]"),
            label({30, -50}, "[@w] [@l]", "upperLeft")},
           {{"w", "1u"}, {"l", "0.13u"}, {"ng", "1"}, {"m", "1"}},
           std::move(models)};
    if (p) {
        c.shapes.push_back(ellipse({-56, -8}, {-40, 8}));
        c.shapes.push_back(line({{-100, 0}, {-56, 0}}, "pin"));
    } else {
        c.shapes.push_back(line({{-100, 0}, {-40, 0}}, "pin"));
    }
    return c;
}

const std::vector<std::string> kMosSections = {"mos_tt", "mos_ss", "mos_ff", "mos_sf", "mos_fs", "mos_tt_stat", "mos_tt_mismatch"};

// OpenVLSI's MOS corner file over the PDK's cornerMOS<kind>.lib: each of its sections, and mos_tt_stat_mismatch, which
// IHP does not ship: its process statistics (mos_tt_stat) with the mismatch model (mos_tt_mismatch's), so one
// Monte Carlo run varies both
void writeMosCorners(const fs::path& models, const std::string& kind, const fs::path& out) {
    const fs::path corner = models / ("cornerMOS" + kind + ".lib");
    std::ifstream in(corner);
    std::vector<std::string> stat;
    bool inside = false, found = false;
    for (std::string l; std::getline(in, l);) {
        std::string t = l;
        t.erase(0, t.find_first_not_of(" \t"));
        std::string up = t;
        for (auto& c : up) c = (char)std::toupper((unsigned char)c);
        if (up.rfind(".LIB MOS_TT_STAT", 0) == 0 && up.find_first_not_of(" \t\r", 16) == std::string::npos) { inside = found = true; continue; }
        if (inside && up.rfind(".ENDL", 0) == 0) break;
        if (!inside) continue;
        if (up.rfind(".INCLUDE", 0) == 0) {  // relative to the PDK's file; the plain MOS model becomes the mismatch pair
            std::string f = t.substr(8);
            f.erase(0, f.find_first_not_of(" \t\"'"));
            f.erase(f.find_last_not_of(" \t\r\"'") + 1);
            const std::string plain = "sg13g2_mos" + kind + "_mod.lib";
            if (f == plain) {
                stat.push_back(".include \"" + (models / ("sg13g2_mos" + kind + "_mismatch.lib")).string() + "\"");
                stat.push_back(".include \"" + (models / ("sg13g2_mos" + kind + "_mod_mismatch.lib")).string() + "\"");
            } else {
                stat.push_back(".include \"" + (models / f).string() + "\"");
            }
            continue;
        }
        stat.push_back(l);
    }
    if (!found || stat.empty()) throw std::invalid_argument("pdk_install: " + corner.string() + " has no mos_tt_stat section");
    std::ofstream o(out);
    o << "* OpenVLSI: SG13G2 " << kind << " MOS corners, over " << corner.string() << "\n"
      << "* mos_tt_stat_mismatch: the process statistics of mos_tt_stat with the mismatch model (one Monte Carlo run, both)\n";
    for (const auto& sec : kMosSections) o << ".LIB " << sec << "\n.lib \"" << corner.string() << "\" " << sec << "\n.ENDL " << sec << "\n";
    o << ".LIB mos_tt_stat_mismatch\n";
    for (const auto& l : stat) o << l << "\n";
    o << ".ENDL mos_tt_stat_mismatch\n";
    if (!o.flush()) throw std::runtime_error("pdk_install: cannot write " + out.string());
}

std::vector<Cell> sg13g2(const fs::path& models, const std::string& osdi, const fs::path& lv, const fs::path& hv) {
    auto m = [&](const char* file, const char* section) { return QJsonObject{{"file", QString::fromStdString((models / file).string())}, {"section", section}}; };
    auto own = [](const fs::path& f) { return QJsonObject{{"file", QString::fromStdString(f.string())}, {"section", "mos_tt"}}; };
    const QJsonObject psp{{"file", QString::fromStdString(osdi)}};
    std::vector<Cell> out = {mos("sg13_lv_nmos", false, "sg13_lv_nmos", {psp, own(lv)}), mos("sg13_lv_pmos", true, "sg13_lv_pmos", {psp, own(lv)}),
                             mos("sg13_hv_nmos", false, "sg13_hv_nmos", {psp, own(hv)}), mos("sg13_hv_pmos", true, "sg13_hv_pmos", {psp, own(hv)})};
    for (const char* r : {"rsil", "rppd", "rhigh"}) {
        Cell c = twoTerminal(r, "X", "", {{"w", "0.5u"}, {"l", "0.5u"}, {"m", "1"}},
                             {{0, "rect", "device", {{-20, -50}, {20, 50}}, "", ""}, line({{20, 0}, {100, 0}}, "pin")}, "[@w] [@l]");
        c.tmpl = std::string("[|PLUS] [|MINUS] [|B] ") + r + " w=[@w] l=[@l] m=[@m] mm_ok={mc_mismatch}";
        c.instPrefix = "R";
        c.pins.push_back(pin("B", "inputOutput", {100, 0}));
        c.models = {m("cornerRES.lib", "res_typ")};
        out.push_back(c);
    }
    Cell cap = twoTerminal("cap_cmim", "X", "[|PLUS] [|MINUS] cap_cmim w=[@w] l=[@l] mm_ok={mc_mismatch}", {{"w", "7u"}, {"l", "7u"}},
                           {line({{-40, 10}, {40, 10}}), line({{-40, -10}, {40, -10}}), line({{0, 50}, {0, 10}}), line({{0, -10}, {0, -50}})},
                           "[@w] [@l]");
    cap.instPrefix = "C";
    cap.models = {m("cornerCAP.lib", "cap_typ")};
    out.push_back(cap);
    return out;
}

// The FinFET and GAA PDKs OpenROAD supports (BSIM-CMG LEVEL 72 VERSION 107 cards, run on bsimcmg107.osdi): ASAP7's fins,
// ASAP5's nanowires. Their cards ship as one file per corner; OpenVLSI's <pdk>.lib makes each corner a section (tt, ss,
// ...), so a primitive names the tt section and an ADE corner swaps it, as with SG13G2.
struct FinPdk {
    const char* name;
    std::vector<std::pair<std::string, std::string>> corners;  // section, the PDK's model file under models/hspice
    const char* length;                                       // the drawn L a cell defaults to
};
const FinPdk& finPdk(const std::string& name) {
    static const FinPdk asap7{"asap7", {{"tt", "7nm_TT_160803.pm"}, {"ss", "7nm_SS_160803.pm"}, {"ff", "7nm_FF_160803.pm"}}, "20n"},
        asap5{"asap5", {{"tt", "asap5_tt.pm"}, {"ss", "asap5_ss.pm"}, {"ff", "asap5_ff.pm"}, {"fs", "asap5_fs.pm"}, {"sf", "asap5_sf.pm"}}, "14n"};
    return name == "asap7" ? asap7 : asap5;
}

// nmos / pmos x slvt / lvt / rvt / sram: the card's own MOSFET (an M primitive), NFIN fins of drawn length L
std::vector<Cell> finfets(const FinPdk& pdk, const std::string& osdi, const fs::path& lib) {
    std::vector<Cell> out;
    for (const char* t : {"nmos", "pmos"})
        for (const char* vt : {"slvt", "lvt", "rvt", "sram"}) {
            const std::string name = std::string(t) + "_" + vt;
            Cell c = mos(name, t[0] == 'p', name, {QJsonObject{{"file", QString::fromStdString(osdi)}},
                                                  QJsonObject{{"file", QString::fromStdString(lib.string())}, {"section", "tt"}}});
            c.prefix = "M";
            c.tmpl = "[|D] [|G] [|S] [|B] " + name + " nfin=[@nfin] l=[@l] m=[@m]";
            c.params = {{"nfin", "2"}, {"l", pdk.length}, {"m", "1"}};
            for (auto& sh : c.shapes) if (sh.text == "[@w] [@l]") sh.text = "nfin=[@nfin] [@l]";
            out.push_back(c);
        }
    return out;
}

void writeFinCorners(const FinPdk& pdk, const fs::path& models, const fs::path& out) {
    std::ofstream o(out);
    o << "* OpenVLSI: " << pdk.name << " corners over " << models.string() << " (BSIM-CMG LEVEL 72 VERSION 107 cards)\n";
    for (const auto& [sec, file] : pdk.corners) o << ".LIB " << sec << "\n.include \"" << (models / file).string() << "\"\n.ENDL " << sec << "\n";
    if (!o.flush()) throw std::runtime_error("pdk_install: cannot write " + out.string());
}

QString install(Context& c, const std::string& name, const std::vector<Cell>& cells, const QJsonObject& globals) {
    const fs::path path = fs::path(c.workspace.file()).parent_path() / (name + ".oalib");
    oadb::Library& l = c.workspace.createLibrary(name, path.string());
    l.change("install " + name, [&] {
        for (const Cell& cell : cells) {
            l.createCell(cell.name);
            l.createView(cell.name, "symbol", "symbol");
            for (const auto& p : cell.pins) l.addPin(cell.name, "symbol", p);
            for (const auto& s : cell.shapes) l.addShape(cell.name, "symbol", s);
            QJsonObject spice{{"prefix", QString::fromStdString(cell.prefix)}, {"template", QString::fromStdString(cell.tmpl)}};
            if (!cell.models.isEmpty()) spice["models"] = cell.models;
            if (!globals.isEmpty()) spice["globals"] = globals;
            auto json = [](const QJsonValue& v) {
                const QByteArray a = QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact);
                return a.mid(1, a.size() - 2).toStdString();
            };
            l.setCellProp(cell.name, "spice", json(spice));
            l.setCellProp(cell.name, "params", json(cell.params));
            l.setCellProp(cell.name, "instNamePrefix", json(QString::fromStdString(cell.instPrefix)));
        }
    });
    return QString::fromStdString(name);
}

// analogLib, unless an earlier pdk_install made it (a workspace with SG13G2 and a FinFET PDK shares one)
QJsonArray withAnalogLib(Context& c) {
    const auto libs = c.workspace.libraries();
    if (std::find(libs.begin(), libs.end(), "analogLib") != libs.end()) return {};
    return {install(c, "analogLib", analogLib(), {})};
}

}  // namespace

void addPdkCommands(Registry& r) {
    r.add({"pdk_install", "Creates the libraries analogLib (ideal sources and passives) and a PDK's device library (sg13g2 for "
                          "IHP SG13G2: lv/hv MOS, MIM cap, poly resistors; asap7 / asap5 for the FinFET and nanowire PDKs: "
                          "nmos/pmos x slvt/lvt/rvt/sram), each cell a symbol with its spice template, defaults and model files; "
                          "returns the libraries and the PDK's model files for corners (and SG13G2's for Monte Carlo).",
           object({{"pdk", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"ihp-sg13g2", "asap7", "asap5"}}, {"description", "the PDK"}}},
                   {"root", param("string", "the PDK's directory: ihp-sg13g2 (holding libs.tech), asap7_pdk_r1p7 or asap5PDK_r0p4 "
                                            "(holding models/hspice)")},
                   {"osdi", param("string", "the compiled model the MOS devices load: psp103.osdi (SG13G2), bsimcmg107.osdi "
                                            "(ASAP7, ASAP5: tests/build_osdi.sh)")}}),
           [](Context& c, const QJsonObject& a) {
               const fs::path root(a["root"].toString().toStdString()), models = root / "libs.tech" / "ngspice" / "models";
               const std::string osdi = fs::absolute(a["osdi"].toString().toStdString()).string();
               const std::string kind = a["pdk"].toString().toStdString();
               if (kind != "ihp-sg13g2") {
                   const FinPdk& pdk = finPdk(kind);
                   const fs::path cards = root / "models" / "hspice";
                   for (const auto& [sec, file] : pdk.corners)
                       if (!fs::is_regular_file(cards / file))
                           throw std::invalid_argument("pdk_install: no " + (cards / file).string() + " (is root the " + kind + " PDK's directory?)");
                   if (!fs::is_regular_file(osdi)) throw std::invalid_argument("pdk_install: no OSDI model at " + osdi);
                   const fs::path lib = fs::absolute(fs::path(c.workspace.file()).parent_path()) / (kind + ".lib");
                   writeFinCorners(pdk, cards, lib);
                   QJsonArray libs = withAnalogLib(c);
                   libs.append(install(c, kind, finfets(pdk, osdi, lib), {}));
                   QJsonArray corners;
                   for (const auto& [sec, file] : pdk.corners) corners.append(QString::fromStdString(sec));
                   c.workspace.save();
                   return QJsonValue(QJsonObject{{"libraries", libs}, {"models", QJsonObject{{"osdi", QString::fromStdString(osdi)},
                                                 {"mos", QString::fromStdString(lib.string())}, {"corners", corners}}}});
               }
               for (const char* f : {"cornerMOSlv.lib", "cornerMOShv.lib", "cornerRES.lib", "cornerCAP.lib"})
                   if (!fs::is_regular_file(models / f)) throw std::invalid_argument("pdk_install: no " + (models / f).string() + " (is root the ihp-sg13g2 directory?)");
               if (!fs::is_regular_file(osdi)) throw std::invalid_argument("pdk_install: no OSDI model at " + osdi);
               const fs::path dir = fs::absolute(fs::path(c.workspace.file()).parent_path());
               const fs::path lv = dir / "sg13g2_lv.lib", hv = dir / "sg13g2_hv.lib";
               writeMosCorners(models, "lv", lv);
               writeMosCorners(models, "hv", hv);
               QJsonArray libs = withAnalogLib(c);
               libs.append(install(c, "sg13g2", sg13g2(models, osdi, lv, hv), {{"mc_mismatch", "0"}}));
               auto path = [](const fs::path& f) { return QString::fromStdString(f.string()); };
               // what a corner or a Monte Carlo run selects: a section of these files, and the mismatch switch
               const QJsonObject pdkModels{{"osdi", QString::fromStdString(osdi)}, {"mos_lv", path(lv)}, {"mos_hv", path(hv)},
                                           {"res", path(models / "cornerRES.lib")}, {"cap", path(models / "cornerCAP.lib")},
                                           {"corners", QJsonArray{"mos_tt", "mos_ss", "mos_ff", "mos_sf", "mos_fs"}},
                                           {"montecarlo", QJsonObject{{"mos", "mos_tt_stat_mismatch"}, {"mos_process", "mos_tt_stat"},
                                                                      {"mos_mismatch", "mos_tt_mismatch"}, {"res", "res_stat"}, {"cap", "cap_typ_stat"},
                                                                      {"mismatch_variable", "mc_mismatch"}}}};
               c.workspace.save();
               return QJsonValue(QJsonObject{{"libraries", libs}, {"models", pdkModels}});
           }});
}

}  // namespace cmd
