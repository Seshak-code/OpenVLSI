// PDK setup: analogLib (ideal sources and passives) and a PDK's device library, as primitives with symbols, spice
// templates, parameter defaults and the model files they need (sim/netlist.h)
#include "cmd/registry.h"
#include "flow/rtl_flow.h"
#include "layout/gds.h"
#include "layout/pcell.h"
#include "layout/tech.h"
#include "oadb/oadb.h"
#include "sch/schematic.h"

#include <QRegularExpression>

#include <QJsonDocument>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
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
        // The resistor cards are r3_cmc's, a compiled model too: the one beside the transistors', when it is there. Without it a
        // resistor still netlists, and a simulation says which model is not loaded.
        const fs::path r3 = fs::path(osdi).parent_path() / "r3_cmc.osdi";
        c.models = {m("cornerRES.lib", "res_typ")};
        if (fs::is_regular_file(r3)) c.models.insert(c.models.begin(), QJsonObject{{"file", QString::fromStdString(r3.string())}});
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

// A CDL file's subcircuits: name, ports in order, the directions its *.PININFO gives, and its element lines
struct CdlCell {
    std::string name, text;
    std::vector<std::string> ports;
    std::map<std::string, char> dirs;          // I, O, B
    std::vector<std::vector<std::string>> lines;  // each element, in words
};

std::vector<CdlCell> readCdl(const fs::path& file) {
    std::ifstream in(file);
    if (!in) throw std::invalid_argument("pdk_install_cells: cannot read " + file.string());
    std::vector<std::string> logical;
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line[0] == '+' && !logical.empty()) logical.back() += " " + line.substr(1);
        else logical.push_back(line);
    }
    std::vector<CdlCell> out;
    CdlCell* at = nullptr;
    for (const auto& line : logical) {
        std::istringstream w(line);
        std::vector<std::string> words;
        for (std::string x; w >> x;) words.push_back(x);
        if (words.empty()) continue;
        std::string head = words[0];
        for (auto& ch : head) ch = (char)std::toupper((unsigned char)ch);
        if (head == ".SUBCKT" && words.size() >= 2) {
            out.push_back({words[1], "", {}, {}, {}});
            at = &out.back();
            for (size_t k = 2; k < words.size(); ++k)
                if (words[k].find('=') == std::string::npos) at->ports.push_back(words[k]);
        }
        if (!at) continue;
        at->text += line + "\n";
        if (head == "*.PININFO") {
            for (size_t k = 1; k < words.size(); ++k) {
                const auto colon = words[k].rfind(':');
                if (colon != std::string::npos && colon + 1 < words[k].size()) at->dirs[words[k].substr(0, colon)] = (char)std::toupper((unsigned char)words[k][colon + 1]);
            }
        } else if (head == ".ENDS") {
            at = nullptr;
        } else if (head[0] != '*' && head[0] != '.') {
            at->lines.push_back(words);
        }
    }
    return out;
}

// A cell's symbol from its ports: inputs on the left, outputs on the right, its supplies (B ports named like one) on top and
// below, other bidirectional ports on the right under the outputs
Cell portSymbol(const CdlCell& cdl) {
    std::vector<std::string> left, right, top, bottom;
    for (const auto& p : cdl.ports) {
        std::string up = p;
        for (auto& ch : up) ch = (char)std::toupper((unsigned char)ch);
        const auto d = cdl.dirs.find(p);
        const char dir = d == cdl.dirs.end() ? 'B' : d->second;
        const bool ground = up.find("VSS") != std::string::npos || up.find("GND") != std::string::npos;
        const bool power = !ground && (up.find("VDD") != std::string::npos || up.find("VCC") != std::string::npos);
        if (dir == 'I') left.push_back(p);
        else if (dir == 'O') right.push_back(p);
        else if (power) top.push_back(p);
        else if (ground) bottom.push_back(p);
        else right.push_back(p);
    }
    const int64_t rows = (int64_t)std::max<size_t>({left.size(), right.size(), 1});
    const int64_t half = std::max<int64_t>(100, rows * 50), w = std::max<int64_t>(200, (int64_t)std::max(top.size(), bottom.size()) * 100 + 100);
    Cell c{cdl.name, "X", "", "X", {}, {}, {}, {}};
    c.shapes.push_back({0, "rect", "device", {{-w / 2, -half}, {w / 2, half}}, "", ""});
    const auto side = [&](const std::vector<std::string>& ps, int64_t x, int64_t lead) {
        for (size_t k = 0; k < ps.size(); ++k) {
            const int64_t y = half - 50 - (int64_t)k * 100 + (rows - (int64_t)ps.size()) * 0;
            const char* dir = cdl.dirs.count(ps[k]) && cdl.dirs.at(ps[k]) == 'I' ? "input" : cdl.dirs.count(ps[k]) && cdl.dirs.at(ps[k]) == 'O' ? "output" : "inputOutput";
            c.pins.push_back({0, ps[k], dir, {x + lead, y}});
            c.shapes.push_back(line({{x, y}, {x + lead, y}}, "pin"));
            c.shapes.push_back(label({x + (lead < 0 ? 10 : -10), y}, ps[k].c_str(), lead < 0 ? "centerLeft" : "centerRight"));
        }
    };
    side(left, -w / 2, -100);
    side(right, w / 2, 100);
    const auto rail = [&](const std::vector<std::string>& ps, int64_t y, int64_t lead) {
        for (size_t k = 0; k < ps.size(); ++k) {
            const int64_t x = -w / 2 + 100 + (int64_t)k * 100;
            c.pins.push_back({0, ps[k], "inputOutput", {x, y + lead}});
            c.shapes.push_back(line({{x, y}, {x, y + lead}}, "pin"));
        }
    };
    rail(top, half, 100);
    rail(bottom, -half, -100);
    c.shapes.push_back(label({-w / 2, half + 20}, "[@instanceName]"));
    c.shapes.push_back(label({-w / 2, -half - 20}, "[@cellName]", "upperLeft"));
    return c;
}

// The cells' netlist for simulation: each transistor through the device library's subcircuit (as a schematic's would be), a
// tie through the PDK's own (ptap1, ntap1), an antenna or ESD diode left out (reverse biased: its capacitance alone).
// ponytail: an IO cell's ESD diodes are left out too, so a pad simulates without its protection's leakage and capacitance.
// A cell whose devices are anything else gets no netlist.
// Returns the cells it could write.
std::set<std::string> writeSimNetlist(const std::vector<CdlCell>& cells, const fs::path& out, const std::set<std::string>* only = nullptr) {
    std::set<std::string> defined;
    for (const auto& c : cells) defined.insert(c.name);
    std::map<std::string, bool> ok;
    std::function<bool(const CdlCell&)> simulable = [&](const CdlCell& c) {
        if (const auto it = ok.find(c.name); it != ok.end()) return it->second;
        ok[c.name] = false;  // a loop is not simulable
        bool good = true;
        for (const auto& w : c.lines) {
            const char e = (char)std::toupper((unsigned char)w[0][0]);
            if (e == 'M') good = good && w.size() >= 6 && w[5].rfind("sg13_", 0) == 0;
            else if (e == 'D') good = good && w.size() >= 4 && (w[3] == "dantenna" || w[3] == "dpantenna");
            else if (e == 'X') {
                std::string sub;
                for (size_t k = w.size(); k-- > 1;)
                    if (w[k].find('=') == std::string::npos) { sub = w[k]; break; }
                if (sub == "ptap1" || sub == "ntap1") continue;  // the PDK's substrate and well ties: its resistor models define them
                const auto child = std::find_if(cells.begin(), cells.end(), [&](const CdlCell& x) { return x.name == sub; });
                good = good && child != cells.end() && simulable(*child);
            } else good = false;
        }
        return ok[c.name] = good;
    };
    std::ofstream o(out);
    o << "* OpenVLSI: " << out.filename().string() << ", the cells' transistors through the device library's subcircuits\n";
    if (!only) o << ".global sub!\n";  // the substrate, which the IO cells' ties and diodes name
    std::set<std::string> written;
    for (const auto& c : cells) {
        if (!simulable(c) || (only && !only->count(c.name))) continue;
        o << ".subckt " << c.name;
        for (const auto& p : c.ports) o << " " << p;
        o << "\n";
        for (const auto& w : c.lines) {
            const char e = (char)std::toupper((unsigned char)w[0][0]);
            if (e == 'D') continue;
            o << (e == 'M' ? "X" + w[0] : w[0]);
            for (size_t k = 1; k < w.size(); ++k) {
                std::string word = w[k];
                for (auto& ch : word) ch = (char)std::tolower((unsigned char)ch);
                o << " " << (e == 'M' && k >= 6 ? word : w[k]);
            }
            if (e == 'M') o << " mm_ok={mc_mismatch}";
            o << "\n";
        }
        o << ".ends " << c.name << "\n";
        written.insert(c.name);
    }
    if (!o.flush()) throw std::runtime_error("pdk_install_cells: cannot write " + out.string());
    return written;
}

}  // namespace

QString q(const std::string& text) { return QString::fromStdString(text); }

// A JSON value as a cell property holds it
// Each macro's pins in a LEF file, as an abstract for LVS: per pin its metal rectangles on the process's GDS layers, in database units
// (dbuPerMicron). A layer the process does not name is refused.
std::map<std::string, QJsonArray> lefAbstracts(const fs::path& file, const layout::Tech& tech, double dbuPerMicron) {
    std::ifstream in(file);
    if (!in) throw std::runtime_error("cannot read " + file.string());
    std::map<std::string, QJsonArray> out;
    std::string word, macro, pin, layerName;
    int layer = -1;
    QJsonArray rects;
    const auto flush = [&] {
        if (!pin.empty() && !rects.isEmpty()) out[macro].append(QJsonObject{{"name", QString::fromStdString(pin)}, {"layer", layer}, {"rects", rects}});
        rects = QJsonArray{};
    };
    bool inPort = false;
    while (in >> word) {
        if (word == "MACRO") { in >> macro; pin.clear(); }
        else if (word == "PIN" && !macro.empty()) { flush(); in >> pin; layer = -1; }
        else if (word == "PORT") inPort = true;
        else if (word == "END") {
            std::string what;
            std::getline(in, what);
            if (inPort && what.find_first_not_of(" \t;\r") == std::string::npos) { flush(); inPort = false; }  // END of a PORT
            else if (!pin.empty() && what.find(pin) != std::string::npos) { flush(); pin.clear(); }
        } else if (word == "LAYER" && inPort) {
            flush();
            in >> layerName;
            const auto it = std::find_if(tech.layers.begin(), tech.layers.end(), [&](const layout::TechLayer& l) { return l.name == layerName && l.datatype == 0; });
            if (it == tech.layers.end()) throw std::runtime_error(file.string() + ": layer " + layerName + " is no layer of " + tech.name);
            layer = it->layer;
        } else if (word == "RECT" && inPort && layer >= 0) {
            double x0, y0, x1, y1;
            in >> x0 >> y0 >> x1 >> y1;
            rects.append(QJsonArray{std::llround(x0 * dbuPerMicron), std::llround(y0 * dbuPerMicron), std::llround(x1 * dbuPerMicron), std::llround(y1 * dbuPerMicron)});
        }
    }
    return out;
}

std::string propText(const QJsonValue& v) {
    const QByteArray b = QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact);
    return b.mid(1, b.size() - 2).toStdString();
}

// A Verilog netlist's ports of a module, bit by bit as CDL names them (req_msg[3]): I, O or B
std::map<std::string, char> verilogDirections(const fs::path& file) {
    std::ifstream in(file);
    if (!in) throw std::runtime_error("cannot read " + file.string());
    static const QRegularExpression decl(R"(^\s*(input|output|inout)\s*(?:\[\s*(\d+)\s*:\s*(\d+)\s*\])?\s*([A-Za-z_][\w$]*)\s*;)");
    std::map<std::string, char> out;
    for (std::string line; std::getline(in, line);) {
        const auto m = decl.match(QString::fromStdString(line));
        if (!m.hasMatch()) continue;
        const char d = m.captured(1) == "input" ? 'I' : m.captured(1) == "output" ? 'O' : 'B';
        const std::string name = m.captured(4).toStdString();
        if (m.captured(2).isEmpty()) {
            out[name] = d;
            continue;
        }
        const int hi = m.captured(2).toInt(), lo = m.captured(3).toInt();
        for (int b = std::min(hi, lo); b <= std::max(hi, lo); ++b) out[name + "[" + std::to_string(b) + "]"] = d;
    }
    return out;
}


// ---- A chip's top routed from its schematic: each pad's core pin to the block pin it joins, the supplies to the block's rings
// ponytail: the shape of a chip layout_pad_ring makes around one flow-built block (IHP's pads and ORFS's grid: rings on TopMetal1 and
// TopMetal2, VDD outside VSS), each pad facing the block side its pin is on; one track a net in each side's channel. A general
// top-level router (Innovus's, OpenROAD's with the block's abstract) is the upgrade.
struct RBox { int64_t x0 = 0, y0 = 0, x1 = 0, y1 = 0; };
RBox placed(const RBox& r, double angle, Point at) {
    const auto t = [&](int64_t x, int64_t y) -> Point {
        if (angle == 90) return {at.x - y, at.y + x};
        if (angle == 180) return {at.x - x, at.y - y};
        if (angle == 270) return {at.x + y, at.y - x};
        return {at.x + x, at.y + y};
    };
    const Point a = t(r.x0, r.y0), b = t(r.x1, r.y1);
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)};
}

QJsonObject routeChip(Context& c, const std::string& lib, const std::string& cell, const std::string& view) {
    oadb::Library& l = c.workspace.library(lib);
    if (l.viewType(cell, view) != "layout") throw std::invalid_argument(lib + "/" + cell + "/" + view + " is no layout view");
    const sch::Connectivity k = sch::extract(c.workspace, lib, cell, "schematic");
    if (!k.ok()) throw std::invalid_argument(lib + "/" + cell + "/schematic does not check: " + k.errors.front());
    const auto parsed = [](const std::optional<std::string>& t) -> QJsonValue { return QJsonDocument::fromJson(QByteArray::fromStdString("[" + t.value_or("null") + "]")).array().at(0); };
    // The schematic: pads (cells with an abstract and a core pin) and the block (one cell with a layout)
    struct SPad { std::string name, master, pad, core, corePin; };
    std::vector<SPad> pads;
    std::string blockLib, blockCell, blockInst;
    for (const auto& i : l.instances(cell, "schematic")) {
        const auto nets = k.instanceNets.count(i.name) ? k.instanceNets.at(i.name) : std::map<std::string, std::string>{};
        oadb::Library& m = c.workspace.library(i.lib);
        if (m.cellProp(i.cell, "abstract")) {
            for (const char* pin : {"p2c", "c2p"})
                if (nets.count(pin) && nets.count("pad")) pads.push_back({i.name, i.cell, nets.at("pad"), nets.at(pin), pin});
            continue;
        }
        bool laid = false;
        for (const auto& v : m.views(i.cell)) laid = laid || v.name == "layout";
        if (!laid) continue;
        if (!blockCell.empty()) throw std::invalid_argument("layout_route_chip routes one block's pins; the schematic places " + blockCell + " and " + i.cell);
        blockLib = i.lib, blockCell = i.cell, blockInst = i.name;
    }
    if (blockCell.empty()) throw std::invalid_argument(cell + "'s schematic places no block with a layout");
    const auto blockNets = k.instanceNets.count(blockInst) ? k.instanceNets.at(blockInst) : std::map<std::string, std::string>{};
    // The layout: the pads (by their bond pads' labels), the block's instance
    const auto shapes = l.layoutShapes(cell, view);
    const auto insts = l.layoutInstances(cell, view);
    const oadb::LayoutInstance* blockAt = nullptr;
    for (const auto& i : insts)
        if (i.cell == blockCell && (i.lib.empty() ? lib : i.lib) == blockLib) {
            if (blockAt) throw std::invalid_argument("the layout places " + blockCell + " twice");
            blockAt = &i;
        }
    if (!blockAt) throw std::invalid_argument("the layout does not place " + blockCell + ": place it first");
    if (blockAt->angle != 0 || blockAt->mirrorX) throw std::invalid_argument("layout_route_chip takes the block unrotated");
    // The block's pins and extent, from its own layout
    oadb::Library& bl = c.workspace.library(blockLib);
    std::map<std::string, std::vector<std::pair<int, RBox>>> bpins;
    RBox bb{INT64_MAX, INT64_MAX, INT64_MIN, INT64_MIN};
    const auto bshapes = bl.layoutShapes(blockCell, "layout");
    for (const auto& sh : bshapes) {
        for (const auto& p : sh.points) bb = {std::min(bb.x0, p.x), std::min(bb.y0, p.y), std::max(bb.x1, p.x), std::max(bb.y1, p.y)};
        if (sh.kind != "text") continue;
        for (const auto& pin : bshapes) {
            if (pin.datatype != 2 || pin.layer != sh.layer || pin.points.size() < 2) continue;
            RBox r{INT64_MAX, INT64_MAX, INT64_MIN, INT64_MIN};
            for (const auto& p : pin.points) r = {std::min(r.x0, p.x), std::min(r.y0, p.y), std::max(r.x1, p.x), std::max(r.y1, p.y)};
            const Point t = sh.points[0];
            if (t.x >= r.x0 && t.x <= r.x1 && t.y >= r.y0 && t.y <= r.y1)
                bpins[sh.text].push_back({sh.layer, {r.x0 + blockAt->at.x, r.y0 + blockAt->at.y, r.x1 + blockAt->at.x, r.y1 + blockAt->at.y}});
        }
    }
    bb = {bb.x0 + blockAt->at.x, bb.y0 + blockAt->at.y, bb.x1 + blockAt->at.x, bb.y1 + blockAt->at.y};
    const layout::Tech& tech = *layout::findTech("sg13g2");
    const auto metal = [&](int layer) {
        for (const auto& m : tech.layers)
            if (m.layer == layer && m.datatype == 0) return m.name;
        throw std::runtime_error("no metal on layer " + std::to_string(layer));
    };
    QJsonArray routed;
    int shapesAdded = 0;
    l.change("route " + cell, [&] {
        const auto rect = [&](const char* name, int64_t x0, int64_t y0, int64_t x1, int64_t y1) {
            const int layer = std::find_if(tech.layers.begin(), tech.layers.end(), [&](const layout::TechLayer& t) { return t.name == name && t.datatype == 0; })->layer;
            l.addLayoutShape(cell, view, layout::rectShape({layer, 0}, {std::min(x0, x1), std::min(y0, y1), std::max(x0, x1), std::max(y0, y1)}));
            ++shapesAdded;
        };
        const auto via = [&](int lower, int64_t x, int64_t y, int cols = 1, int rows = 1) {  // lower: an index into the metals
            for (const auto& sh : layout::viaShapes(tech, lower, x / 5 * 5, y / 5 * 5, cols, rows)) { l.addLayoutShape(cell, view, sh); ++shapesAdded; }
        };
        // Each signal: its pad's core pin and the block pin its net reaches, on the block side the pad faces
        struct Net { std::string name; int side; RBox pad, pin; int pinLayer; int64_t padC, pinC; };
        std::map<int, std::vector<Net>> bySide;  // 0 south, 1 east, 2 north, 3 west
        for (const auto& sp : pads) {
            std::string pinName;
            for (const auto& [pn, net] : blockNets)
                if (net == sp.core) pinName = pn;
            if (pinName.empty()) continue;  // a pad whose core pin reaches no block pin
            if (!bpins.count(pinName)) throw std::invalid_argument("the block's layout has no pin " + pinName);
            // The layout's pad: the one beside its bond pad's label
            const oadb::LayoutShape* label = nullptr;
            for (const auto& sh : shapes)
                if (sh.kind == "text" && sh.text == sp.pad) label = &sh;
            if (!label) throw std::invalid_argument("no bond pad is labelled " + sp.pad + " (layout_pad_ring names them as their pads)");
            const oadb::LayoutInstance* best = nullptr;
            int64_t bestD = INT64_MAX;
            for (const auto& i : insts) {
                if (i.cell != sp.master) continue;
                const RBox r = placed({0, 0, 80000, 180000}, i.angle, i.at);
                const int64_t dx = std::max<int64_t>({r.x0 - label->points[0].x, 0, label->points[0].x - r.x1}), dy = std::max<int64_t>({r.y0 - label->points[0].y, 0, label->points[0].y - r.y1});
                if (dx + dy < bestD) bestD = dx + dy, best = &i;
            }
            if (!best) throw std::invalid_argument("the layout has no " + sp.master + " for pad " + sp.pad);
            RBox core{};
            bool found = false;
            for (const auto& pv : parsed(c.workspace.library("sg13g2_io").cellProp(sp.master, "abstract")).toArray()) {
                const QJsonObject p = pv.toObject();
                if (p["name"].toString().toStdString() != sp.corePin || p["layer"].toInt() != 10) continue;
                for (const auto& rv : p["rects"].toArray()) {
                    const QJsonArray r = rv.toArray();
                    core = placed({(int64_t)r[0].toDouble(), (int64_t)r[1].toDouble(), (int64_t)r[2].toDouble(), (int64_t)r[3].toDouble()}, best->angle, best->at);
                    found = true;
                }
            }
            if (!found) throw std::runtime_error(sp.master + "'s abstract has no Metal2 " + sp.corePin);
            const int side = best->angle == 0 ? 0 : best->angle == 90 ? 1 : best->angle == 180 ? 2 : 3;
            const auto& [pinLayer, pr] = bpins.at(pinName).front();
            const int pinSide = pr.y0 <= bb.y0 ? 0 : pr.x1 >= bb.x1 ? 1 : pr.y1 >= bb.y1 ? 2 : pr.x0 <= bb.x0 ? 3 : -1;
            if (pinSide != side)
                throw std::invalid_argument("pad " + sp.pad + " faces the block's " + std::string(side % 2 ? (side == 1 ? "east" : "west") : (side ? "north" : "south")) +
                                            " side, but its pin " + pinName + " is not on it (place the block's pins facing their pads)");
            const bool vertical = side % 2 == 0;
            bySide[side].push_back({sp.pad, side, core, pr, pinLayer, vertical ? (core.x0 + core.x1) / 2 : (core.y0 + core.y1) / 2,
                                    vertical ? (pr.x0 + pr.x1) / 2 : (pr.y0 + pr.y1) / 2});
        }
        for (auto& [side, nets] : bySide) {
            // Order: a net whose pin's drop runs beside another's pad drop takes the track nearer the block (Kahn's order on those pairs)
            const size_t n = nets.size();
            std::vector<std::set<size_t>> nearer(n);  // nearer[b]: those that must be farther than b
            std::vector<int> indeg(n, 0);
            for (size_t a = 0; a < n; ++a)
                for (size_t b = 0; b < n; ++b)
                    if (a != b && std::llabs(nets[a].padC - nets[b].pinC) < 1500 && nearer[b].insert(a).second) ++indeg[a];
            std::vector<size_t> order, ready;
            for (size_t i = 0; i < n; ++i)
                if (!indeg[i]) ready.push_back(i);
            while (!ready.empty()) {
                const size_t b = ready.back();
                ready.pop_back();
                order.push_back(b);
                for (const size_t a : nearer[b])
                    if (!--indeg[a]) ready.push_back(a);
            }
            if (order.size() != n) throw std::invalid_argument("the pads and pins of one side cross each other both ways: no track order routes them");
            // The channel between the block and the ring's inner edge (the pads' core pins end there)
            const int64_t edge = side == 0 ? bb.y0 : side == 1 ? bb.x1 : side == 2 ? bb.y1 : bb.x0;
            const int64_t ring = side == 0 ? nets[0].pad.y1 : side == 1 ? nets[0].pad.x0 : side == 2 ? nets[0].pad.y0 : nets[0].pad.x1;
            const int64_t dir = side == 0 || side == 3 ? -1 : 1;  // from the block toward the ring
            if ((int64_t)n * 2000 + 10000 > std::llabs(ring - edge)) throw std::invalid_argument("the channel between the block and the pads is too narrow for its tracks");
            for (size_t rank = 0; rank < n; ++rank) {
                const Net& w = nets[order[rank]];
                const int64_t t = edge + dir * (5000 + 2000 * (int64_t)rank);  // its track
                if (side % 2 == 0) {
                    // The pad's Metal2 pin down (or up) to the track, Metal3 along it, Metal2 onto the block's pin
                    const int64_t px = w.padC / 10 * 10, X = w.pinC / 10 * 10;
                    rect("Metal2", w.pad.x0 + (w.pad.x1 - w.pad.x0 > 400 ? (w.pad.x1 - w.pad.x0 - 400) / 2 : 0), side == 0 ? w.pad.y1 - 2000 : t - 200,
                         w.pad.x1 - (w.pad.x1 - w.pad.x0 > 400 ? (w.pad.x1 - w.pad.x0 - 400) / 2 : 0), side == 0 ? t + 200 : w.pad.y0 + 2000);
                    via(1, px, t);
                    rect("Metal3", px, t - 200, X, t + 200);
                    via(1, X, t);
                    rect("Metal2", X - 100, side == 0 ? t : w.pin.y1 - 300, X + 100, side == 0 ? w.pin.y0 + 300 : t);
                } else {
                    // The pad's Metal2 stub out of the ring, Metal3 to the track's column, Metal2 along it, Metal3 onto the block's pin
                    if (metal(w.pinLayer) != "Metal3") throw std::invalid_argument("pin " + w.name + "'s block pin is on " + metal(w.pinLayer) + "; a side pin is routed on Metal3");
                    const int64_t py = w.padC / 10 * 10, Y = w.pinC / 10 * 10, out = ring - dir * 1500;
                    rect("Metal2", side == 1 ? out - 300 : w.pad.x1 - 2000, py - 200, side == 1 ? w.pad.x0 + 2000 : out + 300, py + 200);
                    via(1, out, py);
                    rect("Metal3", out, py - 200, t, py + 200);
                    via(1, t, py);
                    rect("Metal2", t - 200, std::min(py, Y) - 200, t + 200, std::max(py, Y) + 200);
                    via(1, t, Y);
                    rect("Metal3", side == 1 ? w.pin.x0 + 300 : t, Y - 200, side == 1 ? t : w.pin.x1 - 300, Y + 200);
                }
                routed.append(q(w.name));
            }
        }
        // The supplies: on the west, from the ring's innermost rail of each net onto the block's ring of it (TopMetal1, its left side). The
        // outer ring is reached on TopMetal1 from a stack in the channel; the inner one over TopMetal2 across the outer.
        std::map<std::string, RBox> rings;  // block supply pin -> its TopMetal1 ring on the left
        for (const auto& [pn, list] : bpins)
            for (const auto& [ly, r] : list)
                if (ly == 126 && r.y1 - r.y0 > r.x1 - r.x0 && r.x0 < bb.x0 + 30000) rings[pn] = r;
        if (rings.size() == 2) {
            const auto inner = std::max_element(rings.begin(), rings.end(), [](auto& a, auto& b) { return a.second.x0 < b.second.x0; });
            const auto outer = std::min_element(rings.begin(), rings.end(), [](auto& a, auto& b) { return a.second.x0 < b.second.x0; });
            // The ring's innermost rail per metal: from a supply pad's abstract, the pin whose rect lies 160 to 178 um in
            std::map<int, std::string> innermost;
            for (const auto& pv : parsed(c.workspace.library("sg13g2_io").cellProp("sg13g2_IOPadVdd", "abstract")).toArray()) {
                const QJsonObject p = pv.toObject();
                for (const auto& rv : p["rects"].toArray())
                    if (rv.toArray()[1].toDouble() >= 159000 && rv.toArray()[3].toDouble() <= 179000 && rv.toArray()[2].toDouble() - rv.toArray()[0].toDouble() >= 79000)
                        innermost[p["layer"].toInt()] = p["name"].toString().toStdString();
            }
            const int64_t railX = 305000, mid = (bb.y0 + bb.y1) / 2;  // the west rails sit 300 to 318 um in
            const auto netOf = [&](const std::string& pin) { return blockNets.count(pin) ? blockNets.at(pin) : std::string(); };
            const auto tv2 = [&](int64_t x0, int64_t y0, int cols, int rows) {  // TopVia2: 0.9 um cuts 2 um apart
                for (int i = 0; i < cols; ++i)
                    for (int j = 0; j < rows; ++j) rect("TopVia2", x0 + 2000 * i, y0 + 2000 * j, x0 + 2000 * i + 900, y0 + 2000 * j + 900);
            };
            {  // the outer ring
                const std::string net = netOf(outer->first);
                int railMetal = -1;
                for (const auto& [ly, nm] : innermost)
                    if (nm == net && railMetal < 0 && ly != 126) railMetal = ly;
                if (railMetal < 0) throw std::invalid_argument("no rail of the pad ring carries " + net);
                const int64_t y = mid + 40000, xs = railX + 45000;
                int mi = 0;
                while (tech.metals[mi].layer != railMetal) ++mi;
                rect(metal(railMetal).c_str(), railX, y - 5000, xs + 2000, y + 5000);
                for (int up = mi; up < (int)tech.metals.size() - 1; ++up) {
                    via(up, xs, y, 4, 8);
                    rect(tech.metals[up].name.c_str(), xs - 2000, y - 5000, xs + 2000, y + 5000);
                }
                rect("TopMetal1", xs - 2000, y - 5000, (outer->second.x0 + outer->second.x1) / 2 + 2000, y + 5000);
            }
            {  // the inner ring: from the ring's innermost TopMetal1 rail, over TopMetal2
                const std::string net = netOf(inner->first);
                if (!innermost.count(126) || innermost.at(126) != net) throw std::invalid_argument("the pad ring's innermost TopMetal1 rail does not carry " + net);
                const int64_t y = mid;
                const RBox o = outer->second, i = inner->second;
                rect("TopMetal1", railX, y - 5000, o.x0 - 3000, y + 5000);
                tv2(o.x0 - 9500, y - 3900, 3, 4);
                rect("TopMetal2", o.x0 - 10000, y - 5000, i.x1, y + 5000);
                tv2((i.x0 + i.x1) / 2 - 1450, y - 3900, 2, 4);
            }
            routed.append(q(netOf(outer->first)));
            routed.append(q(netOf(inner->first)));
        }
    });
    return QJsonObject{{"routed", routed}, {"shapes", shapesAdded}};
}

void addPdkCommands(Registry& r) {
    r.add({"flow_make_macro", "Makes a design the flow built into a cell to place by hand (a hard macro): its layout from the run's final GDS, placing the "
                              "standard cells of sg13g2_stdcell (pdk_install_cells) rather than copies, its own via and fill cells named <cell>__<name>; a "
                              "symbol from its Verilog ports; its CDL as LVS's reference; a netlist on the standard cells' for simulation. Returns the "
                              "library and cell, its pins and how many standard cells it places.",
           object({{"run", param("string", "an implementation run: <design>/run.N")}},
                  {{"library", param("string", "the library to put it in, made if need be (default: the design's name)")},
                   {"cell", param("string", "the cell's name (default: the design's)")}}),
           [](Context& c, const QJsonObject& a) {
               static const QRegularExpression runName("^[A-Za-z0-9_]+/run\\.[0-9]+$");
               if (!runName.match(a["run"].toString()).hasMatch()) throw std::invalid_argument("an implementation run is named <design>/run.N");
               const fs::path work = fs::absolute(fs::path(c.workspace.file()).parent_path());
               const fs::path dir = work / "implementation" / a["run"].toString().toStdString();
               std::ifstream recFile(dir / "run.json");
               if (!recFile) throw std::invalid_argument("no implementation run " + a["run"].toString().toStdString());
               const QJsonObject rec = QJsonDocument::fromJson(QByteArray::fromStdString(std::string((std::istreambuf_iterator<char>(recFile)), std::istreambuf_iterator<char>()))).object();
               const std::string design = rec["design"].toString().toStdString(), top = rec["top"].toString().toStdString(), platform = rec["platform"].toString().toStdString();
               if (platform != "ihp-sg13g2") throw std::invalid_argument("a hard macro is made on ihp-sg13g2, whose standard cells pdk_install_cells brings in; this run is " + platform + "'s");
               const std::string cells = "sg13g2_stdcell";
               const auto known = c.workspace.libraries();
               if (std::find(known.begin(), known.end(), cells) == known.end()) throw std::invalid_argument("flow_make_macro places the standard cells of " + cells + ": run pdk_install_cells for stdcell first");
               const fs::path results = dir / "results" / platform / design / "base";
               const fs::path gds = results / "6_final.gds", cdlFile = results / "6_lvs.cdl", verilog = results / "6_final.v";
               for (const auto& f : {gds, cdlFile, verilog})
                   if (!fs::exists(f)) throw std::runtime_error("this run has no " + f.filename().string() + ": it did not finish (flow_status)");
               const std::string library = a.contains("library") ? a["library"].toString().toStdString() : design;
               const std::string cell = a.contains("cell") ? a["cell"].toString().toStdString() : design;
               if (!oadb::validName(cell) || !oadb::validName(library)) throw std::invalid_argument("a library's and a cell's names are letters, digits and _");

               // The layout: the PDK's cells by reference (the standard cells, and the SRAM macros and IO cells when their libraries are
               // installed), the rest renamed under the cell
               oadb::Library& std = c.workspace.library(cells);
               std::map<std::string, std::string> standard;  // cell -> its library
               for (const std::string pdkLib : {cells, std::string("sg13g2_sram"), std::string("sg13g2_io")}) {
                   if (std::find(known.begin(), known.end(), pdkLib) == known.end()) continue;
                   oadb::Library& from = c.workspace.library(pdkLib);
                   for (const auto& name : from.cells())
                       for (const auto& v : from.views(name))
                           if (v.name == "layout") standard.emplace(name, pdkLib);
               }
               layout::GdsLibrary g = layout::readGds(gds.string());
               std::map<std::string, std::string> renamed;
               bool hasTop = false;
               for (const auto& gc : g.cells) {
                   hasTop = hasTop || gc.name == top;
                   if (!standard.count(gc.name)) renamed[gc.name] = gc.name == top ? cell : cell + "__" + gc.name;
               }
               if (!hasTop) throw std::runtime_error("the run's GDS has no structure " + top);
               if (std::find(known.begin(), known.end(), library) == known.end()) {
                   oadb::Library& made = c.workspace.createLibrary(library, (work / (library + ".oalib")).string());
                   made.setLibraryProp("tech", "\"sg13g2\"");
               }
               oadb::Library& l = c.workspace.library(library);
               for (const auto& existing : l.cells())
                   for (const auto& [from, to] : renamed)
                       if (existing == to) throw std::invalid_argument(library + " already has a cell " + to);
               long placedStandard = 0;
               for (auto& gc : g.cells) {
                   if (!renamed.count(gc.name)) continue;
                   gc.name = renamed[gc.name];
                   for (auto& i : gc.instances) {
                       if (standard.count(i.cell)) {
                           i.lib = standard.at(i.cell);
                           placedStandard += i.lib == cells;
                       } else {
                           i.cell = renamed.at(i.cell);
                       }
                   }
               }

               // The netlists: LVS's reference, and the simulation's
               std::vector<CdlCell> netlist = readCdl(cdlFile);
               const auto topCdl = std::find_if(netlist.begin(), netlist.end(), [&](const CdlCell& x) { return x.name == top; });
               if (topCdl == netlist.end()) throw std::runtime_error("the run's CDL has no .SUBCKT " + top);
               CdlCell me = *topCdl;
               me.dirs.clear();
               for (const auto& [port, d] : verilogDirections(verilog)) me.dirs[port] = d;
               for (const auto& p : me.ports)
                   if (!me.dirs.count(p) && p != "VDD" && p != "VSS") throw std::runtime_error("port " + p + " of the CDL is not in the routed Verilog");
               std::string text;
               for (const auto& x : netlist) {
                   if (x.name == top) continue;
                   text += x.text;
               }
               // The top under the cell's name
               std::string own = me.text;
               own.replace(own.find(top), top.size(), cell);
               text += own;
               me.name = cell;
               const fs::path sim = work / (cell + ".sp");
               std::vector<CdlCell> simCells = netlist;
               for (auto& x : simCells)
                   if (x.name == top) x.name = cell;
               const std::set<std::string> only{cell};
               // A block holding a macro with no transistor-level model (an SRAM's) has no SPICE netlist: it is tested through its Verilog
               const bool simulates = writeSimNetlist(simCells, sim, &only).count(cell) > 0;
               if (!simulates) fs::remove(sim);
               QJsonArray models{QJsonObject{{"file", QString::fromStdString(sim.string())}}};
               const QJsonObject stdSpice = QJsonDocument::fromJson(QByteArray::fromStdString("[" + std.cellProp("sg13g2_inv_1", "spice").value_or("{}") + "]")).array()[0].toObject();
               for (const auto& m : stdSpice["models"].toArray())
                   if (!models.contains(m)) models.append(m);
               std::string tmpl;
               for (const auto& p : me.ports) tmpl += "[|" + p + "] ";
               const Cell sym = portSymbol(me);

               l.change("macro " + cell, [&] {
                   for (const auto& gc : g.cells) {
                       if (!renamed.count(gc.name) && gc.name != cell && gc.name.rfind(cell + "__", 0) != 0) continue;
                       l.createCell(gc.name);
                       l.createView(gc.name, "layout", "layout");
                       l.setProp(gc.name, "layout", "metersPerDbu", QString::number(g.metersPerDbu, 'g', 17).toStdString());
                       l.addLayoutObjects(gc.name, "layout", gc.shapes, gc.instances);
                   }
                   l.createView(cell, "symbol", "symbol");
                   for (const auto& p : sym.pins) l.addPin(cell, "symbol", p);
                   for (const auto& s : sym.shapes) l.addShape(cell, "symbol", s);
                   l.setCellProp(cell, "cdl", propText(QString::fromStdString(text)));
                   if (simulates)
                       l.setCellProp(cell, "spice", propText(QJsonObject{{"prefix", "X"}, {"template", QString::fromStdString(tmpl + cell)}, {"models", models}, {"globals", stdSpice["globals"]}}));
                   l.setCellProp(cell, "params", "{}");
                   l.setCellProp(cell, "instNamePrefix", propText(QString("X")));
                   l.setCellProp(cell, "macro", propText(QJsonObject{{"run", a["run"]}, {"design", q(design)}, {"top", q(top)}}));
                   // Its Verilog for a test of a chip that places it (netlist_verilog): the routed netlist and the models of the cells it places
                   QJsonArray verilogFiles{q(verilog.string())};
                   std::set<std::string> usedLibs;
                   for (const auto& gc : g.cells)
                       for (const auto& i : gc.instances)
                           if (!i.lib.empty() && usedLibs.insert(i.lib + "/" + i.cell).second)
                               if (const auto files = c.workspace.library(i.lib).cellProp(i.cell, "verilog"))
                                   for (const auto& f : QJsonDocument::fromJson(QByteArray::fromStdString("[" + *files + "]")).array()[0].toArray())
                                       if (!verilogFiles.contains(f)) verilogFiles.append(f);
                   l.setCellProp(cell, "verilog", propText(verilogFiles));
               });
               c.workspace.save();
               QJsonArray pins;
               for (const auto& p : me.ports) pins.append(q(p));
               return QJsonValue(QJsonObject{{"library", q(library)}, {"cell", q(cell)}, {"pins", pins}, {"standard_cells", (double)placedStandard},
                                             {"structures", (int)renamed.size()}, {"netlist", simulates ? QJsonValue(q(sim.string())) : QJsonValue()}});
           }});
    r.add({"pdk_install_cells", "Brings a set of the PDK's cells into the workspace as a library: the standard cells (sg13g2_stdcell), the IO cells (sg13g2_io) "
                                "or the SRAM macros (sg13g2_sram). Each cell gets its layout from the PDK's GDS, a symbol from its CDL ports (inputs left, outputs "
                                "right, supplies top and bottom), its CDL as LVS's reference, and, when its devices are the device library's MOS, a netlist for "
                                "simulation. pdk_install's sg13g2 must be in the workspace. Returns the library, its cells and those that simulate.",
           object({{"set", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"stdcell", "io", "sram"}}, {"description", "which cells"}}}}),
           [](Context& c, const QJsonObject& a) {
               const flow::Platform* pdk = flow::findPlatform("ihp-sg13g2");
               const auto tools = acutesim::compute::orchestration::RtlFlowTools::detect();
               if (!pdk || tools.flowDir.empty()) throw std::runtime_error("pdk_install_cells reads the PDK's cells from OpenROAD-flow-scripts' ihp-sg13g2 platform: " + tools.missing());
               const auto known = c.workspace.libraries();
               if (std::find(known.begin(), known.end(), "sg13g2") == known.end()) throw std::invalid_argument("pdk_install_cells needs the device library: run pdk_install for ihp-sg13g2 first");
               const std::string set = a["set"].toString().toStdString(), name = "sg13g2_" + set;
               if (std::find(known.begin(), known.end(), name) != known.end()) throw std::invalid_argument("the workspace already has " + name);
               const fs::path platform = fs::path(flow::platformFile(*pdk, tools.flowDir, "cdl")).parent_path();
               std::vector<std::pair<fs::path, fs::path>> files;  // cdl, gds
               if (set == "sram") {
                   for (const auto& e : fs::directory_iterator(platform / "cdl"))
                       if (e.path().filename().string().rfind("RM_IHPSG13", 0) == 0 && e.path().extension() == ".cdl")
                           files.push_back({e.path(), platform / "gds" / (e.path().stem().string() + ".gds")});
                   std::sort(files.begin(), files.end());
               } else {
                   files.push_back({platform / "cdl" / ("sg13g2_" + set + ".cdl"), platform / "gds" / ("sg13g2_" + set + ".gds")});
               }
               for (const auto& [cdl, gds] : files)
                   for (const auto& f : {cdl, gds})
                       if (!fs::is_regular_file(f)) throw std::runtime_error("pdk_install_cells: the platform has no " + f.string());
               // The SRAM macros' files each carry their shared subcells: one of each is kept, and two that differ are refused
               std::vector<CdlCell> cells;
               std::map<std::string, std::string> first;
               for (const auto& [cdl, gds] : files)
                   for (auto& cell : readCdl(cdl)) {
                       const auto [at, fresh] = first.emplace(cell.name, cell.text);
                       if (!fresh) {
                           if (at->second != cell.text) throw std::runtime_error("pdk_install_cells: two files define " + cell.name + " differently");
                           continue;
                       }
                       cells.push_back(std::move(cell));
                   }
               const fs::path dir = fs::absolute(fs::path(c.workspace.file()).parent_path());
               const fs::path sim = dir / (name + ".sp");
               const std::set<std::string> simulates = writeSimNetlist(cells, sim);
               // What a transistor needs when it simulates, as the device library's own cells say
               oadb::Library& devices = c.workspace.library("sg13g2");
               QJsonArray models{QJsonObject{{"file", QString::fromStdString(sim.string())}}};
               QJsonObject globals;
               for (const char* mos : {"sg13_lv_nmos", "sg13_hv_nmos", "rsil"}) {  // rsil's: the resistor models, which define the ties
                   const QJsonObject spice = QJsonDocument::fromJson(QByteArray::fromStdString("[" + devices.cellProp(mos, "spice").value_or("{}") + "]")).array()[0].toObject();
                   for (const auto& m : spice["models"].toArray())
                       if (!models.contains(m)) models.append(m);
                   const QJsonObject g = spice["globals"].toObject();
                   for (auto it = g.begin(); it != g.end(); ++it) globals[it.key()] = it.value();
               }
               oadb::Library& l = c.workspace.createLibrary(name, (dir / (name + ".oalib")).string());
               l.setLibraryProp("tech", "\"sg13g2\"");
               QJsonArray imported;
               for (const auto& [cdl, gds] : files) imported.append(importGds(l, gds.string(), flow::platformFile(*pdk, tools.flowDir, pdk->layerProperties), true));
               if (set == "io") {  // the bond pad a pad ring sets on each pad: metal only, with no netlist
                   const fs::path bond = platform / "gds" / "bondpad_70x70.gds";
                   if (!fs::is_regular_file(bond)) throw std::runtime_error("pdk_install_cells: the platform has no " + bond.string());
                   imported.append(importGds(l, bond.string(), flow::platformFile(*pdk, tools.flowDir, pdk->layerProperties), true));
               }
               const auto json = [](const QJsonValue& v) {
                   const QByteArray b = QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact);
                   return b.mid(1, b.size() - 2).toStdString();
               };
               std::set<std::string> have;
               for (const auto& cl : l.cells()) have.insert(cl);
               std::map<std::string, const CdlCell*> byName;
               for (const auto& cell : cells) byName[cell.name] = &cell;
               // A cell's LVS netlist: its .SUBCKT and those it places
               std::function<void(const CdlCell&, std::set<std::string>&, std::string&)> withChildren = [&](const CdlCell& cell, std::set<std::string>& seen, std::string& text) {
                   if (!seen.insert(cell.name).second) return;
                   for (const auto& w : cell.lines)
                       if (std::toupper((unsigned char)w[0][0]) == 'X')
                           for (size_t k = w.size(); k-- > 1;)
                               if (w[k].find('=') == std::string::npos) {
                                   if (const auto child = byName.find(w[k]); child != byName.end()) withChildren(*child->second, seen, text);
                                   break;
                               }
                   text += cell.text;
               };
               // The black boxes' abstracts: their LEF pins (IHP's IO cells' own labels and ESD structures do not extract as their netlists say)
               std::map<std::string, QJsonArray> abstracts;
               if (set != "stdcell") {
                   const layout::Tech* tech = layout::findTech("sg13g2");
                   std::vector<fs::path> lefs;
                   if (set == "io") lefs.push_back(platform / "lef" / "sg13g2_io.lef");
                   else for (const auto& [cdl, gds] : files) lefs.push_back(platform / "lef" / (cdl.stem().string() + ".lef"));
                   for (const auto& lef : lefs) {
                       const std::string top = lef.stem() == "sg13g2_io" ? "sg13g2_IOPadIn" : lef.stem().string();
                       const double dbuPerMicron = 1e-6 / std::stod(l.prop(top, "layout", "metersPerDbu").value_or("1e-9"));
                       for (auto& [m, pins] : lefAbstracts(lef, *tech, dbuPerMicron)) abstracts[m] = pins;
                   }
               }
               // Each cell's Verilog model, for a test of a chip that places it (netlist_verilog): the standard cells' gate models, the IO
               // cells', an SRAM macro's wrapper and IHP's behavioural core when it is in ~/eda/ihp-verilog
               std::map<std::string, QJsonArray> verilogOf;
               {
                   const char* home = std::getenv("HOME");
                   const auto expand = [&](const std::string& f) { return f.rfind("~/", 0) == 0 ? std::string(home ? home : "") + f.substr(1) : f; };
                   QJsonArray files;
                   if (set == "stdcell") {
                       // The gate models with their delayed_* pins assigned (functionalCellModels), beside the workspace; the rest as they are
                       for (size_t k = 0; k < pdk->cellVerilog.size(); ++k) {
                           const std::string f = expand(pdk->cellVerilog[k]);
                           if (k == 0) {
                               std::ifstream in(f);
                               if (!in) throw std::runtime_error("pdk_install_cells: cannot read the cells' Verilog " + f);
                               const fs::path out = dir / (name + "_functional.v");
                               std::ofstream(out) << acutesim::compute::orchestration::functionalCellModels(
                                   std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()));
                               files.append(q(out.string()));
                           } else {
                               files.append(q(f));
                           }
                       }
                   }
                   if (set == "io") files.append(q((platform / "verilog" / "sg13g2_io.v").string()));
                   for (const auto& cell : cells) {
                       QJsonArray mine = files;
                       if (set == "sram" && cell.name.rfind("RM_IHPSG13", 0) == 0) {
                           mine.append(q((platform / "verilog" / (cell.name + ".v")).string()));
                           const std::string core = expand("~/eda/ihp-verilog/RM_IHPSG13_1P_core_behavioral_bm_bist.v");
                           if (fs::is_regular_file(core)) mine.append(q(core));
                       }
                       if (!mine.isEmpty()) verilogOf[cell.name] = mine;
                   }
               }
               QJsonArray names, simulated;
               l.change("install " + name, [&] {
                   for (const auto& cell : cells) {
                       if (!have.count(cell.name)) l.createCell(cell.name);
                       const Cell sym = portSymbol(cell);
                       l.createView(cell.name, "symbol", "symbol");
                       for (const auto& p : sym.pins) l.addPin(cell.name, "symbol", p);
                       for (const auto& s : sym.shapes) l.addShape(cell.name, "symbol", s);
                       std::set<std::string> seen;
                       std::string text;
                       withChildren(cell, seen, text);
                       l.setCellProp(cell.name, "cdl", json(QString::fromStdString(text)));
                       l.setCellProp(cell.name, "params", "{}");
                       l.setCellProp(cell.name, "instNamePrefix", json(QString("X")));
                       if (const auto vf = verilogOf.find(cell.name); vf != verilogOf.end()) l.setCellProp(cell.name, "verilog", json(vf->second));
                       if (set != "stdcell") {  // compared by its pins in a top's LVS: its abstract stands for it there
                           l.setCellProp(cell.name, "lvs", json(QString("blackbox")));
                           if (const auto ab = abstracts.find(cell.name); ab != abstracts.end()) {
                               // Each pin as the cell's CDL names it (LVS pairs a black box's pins by name): IHP's SRAM LEFs write A_ADDR[0], its
                               // CDL A_ADDR<0>
                               const std::set<std::string> ports(cell.ports.begin(), cell.ports.end());
                               QJsonArray pins;
                               for (auto pv : ab->second) {
                                   QJsonObject p = pv.toObject();
                                   const std::string n = p["name"].toString().toStdString();
                                   std::string angled = std::regex_replace(n, std::regex(R"(\[(\d+)\])"), "<$1>");
                                   if (!ports.count(n) && ports.count(angled)) p["name"] = q(angled);
                                   pins.append(p);
                               }
                               l.setCellProp(cell.name, "abstract", json(pins));
                           }
                       }
                       if (simulates.count(cell.name)) {
                           std::string tmpl;
                           for (const auto& p : cell.ports) tmpl += "[|" + p + "] ";
                           l.setCellProp(cell.name, "spice", json(QJsonObject{{"prefix", "X"}, {"template", QString::fromStdString(tmpl + cell.name)}, {"models", models}, {"globals", globals}}));
                           simulated.append(QString::fromStdString(cell.name));
                       }
                       names.append(QString::fromStdString(cell.name));
                   }
               });
               c.workspace.save();
               return QJsonValue(QJsonObject{{"library", QString::fromStdString(name)}, {"cells", names}, {"simulate", simulated}, {"layouts", imported}, {"netlist", QString::fromStdString(sim.string())}});
           }});
    r.add({"layout_pad_ring", "Places a pad ring of sg13g2_io's cells in a layout: a corner at each corner of the die, the pads of each side spread evenly "
                              "(their bond pad side out), fillers in every gap so the ring's supply rails run unbroken, a bond pad on each pad, and a pin "
                              "on each bond pad named as its pad. The ring sits 140 um inside the die's edge (a 70 um bond pad and 70 um for a seal ring), "
                              "as ORFS's pad.tcl places it. Sides run left to right (south, north) and bottom to top (east, west). Returns the core's box.",
           object({{"library", param("string", "the library")}, {"cell", param("string", "the cell")},
                   {"die", param("array", "the die's width and height, [w, h] in microns (whole microns)")}},
                  {{"view", param("string", "the layout view (default layout)")},
                   {"south", param("array", "pads along the bottom: [{name, master}], master a pad cell of sg13g2_io such as sg13g2_IOPadIn")},
                   {"east", param("array", "pads up the right side")}, {"north", param("array", "pads along the top")}, {"west", param("array", "pads up the left side")}}),
           [](Context& c, const QJsonObject& a) {
               // ponytail: IHP's sg13g2_io only, at its sizes (pads 80 x 180 um, corners 180 um, fillers 1 to 50 um); no seal ring is drawn
               constexpr int64_t um = 1000, pad = 80 * um, depth = 180 * um, offset = 140 * um, bond = 70 * um;
               const std::string io = "sg13g2_io", lib = a["library"].toString().toStdString(), cell = a["cell"].toString().toStdString();
               const std::string view = a.contains("view") ? a["view"].toString().toStdString() : "layout";
               const auto isLayout = [](const oadb::Library& in, const std::string& cl, const std::string& v) {
                   try { return in.viewType(cl, v) == "layout"; } catch (const std::exception&) { return false; }
               };
               oadb::Library& l = c.workspace.library(lib);
               if (!isLayout(l, cell, view)) throw std::invalid_argument(lib + "/" + cell + "/" + view + " is no layout view");
               oadb::Library& cells = c.workspace.library(io);  // throws when pdk_install_cells io has not run
               for (const char* need : {"sg13g2_Corner", "bondpad_70x70"})
                   if (!isLayout(cells, need, "layout")) throw std::invalid_argument(io + " has no " + need + ": run pdk_install_cells io again");
               const QJsonArray die = a["die"].toArray();
               const auto whole = [](const QJsonValue& v) {
                   const double d = v.toDouble(-1);
                   if (d <= 0 || d != std::floor(d)) throw std::invalid_argument("the die's sides are whole microns");
                   return (int64_t)d * um;
               };
               if (die.size() != 2) throw std::invalid_argument("die is [w, h] in microns");
               const int64_t w = whole(die[0]), h = whole(die[1]);
               struct Pad { std::string name, master; };
               struct Side { const char* key; int64_t length; double angle; };
               const std::vector<Side> sides = {{"south", w, 0}, {"east", h, 90}, {"north", w, 180}, {"west", h, 270}};
               std::set<std::string> names;
               std::map<std::string, std::vector<Pad>> pads;
               for (const auto& s : sides)
                   for (const auto& v : a[s.key].toArray()) {
                       const QJsonObject o = v.toObject();
                       Pad p{o["name"].toString().toStdString(), o["master"].toString().toStdString()};
                       if (p.name.empty() || p.master.empty()) throw std::invalid_argument(std::string("each pad of ") + s.key + " has a name and a master");
                       if (!names.insert(p.name).second) throw std::invalid_argument("two pads are named " + p.name);
                       if (p.master.rfind("sg13g2_IOPad", 0) != 0 || !isLayout(cells, p.master, "layout"))
                           throw std::invalid_argument(p.master + " is no pad cell of " + io);
                       pads[s.key].push_back(p);
                   }
               // Where a cell of `along` microns starting `x` along a side goes: its origin and the side's rotation
               const auto place = [&](const Side& s, int64_t x, int64_t along) -> std::pair<Point, double> {
                   if (s.angle == 0) return {{x, offset}, 0};
                   if (s.angle == 90) return {{w - offset, x}, 90};
                   if (s.angle == 180) return {{x + along, h - offset}, 180};
                   return {{offset, x + along}, 270};
               };
               const std::vector<std::pair<std::string, int64_t>> fillers = {{"sg13g2_Filler10000", 50}, {"sg13g2_Filler4000", 20}, {"sg13g2_Filler2000", 10},
                                                                            {"sg13g2_Filler1000", 5}, {"sg13g2_Filler400", 2}, {"sg13g2_Filler200", 1}};
               int placed = 0, filled = 0;
               l.change("pad ring in " + cell, [&] {
                   const auto add = [&](const std::string& master, Point at, double angle) {
                       oadb::LayoutInstance i;
                       i.lib = io;
                       i.cell = master;
                       i.at = at;
                       i.angle = angle;
                       l.addLayoutInstance(cell, view, i);
                   };
                   const Point corners[] = {{offset, offset}, {w - offset, offset}, {w - offset, h - offset}, {offset, h - offset}};
                   for (int k = 0; k < 4; ++k) add("sg13g2_Corner", corners[k], 90.0 * k);
                   for (const auto& s : sides) {
                       const int64_t start = offset + depth, end = s.length - offset - depth;
                       const auto& row = pads[s.key];
                       const int64_t room = end - start - (int64_t)row.size() * pad;
                       if (room < 0) throw std::invalid_argument(std::string("the ") + s.key + " side holds " + std::to_string((end - start) / pad) + " pads at most");
                       // Evenly spread, each gap whole microns so the fillers close it exactly
                       int64_t x = start;
                       const auto fill = [&](int64_t to) {
                           for (const auto& [master, size] : fillers)
                               while (to - x >= size * um) {
                                   const auto [at, angle] = place(s, x, size * um);
                                   add(master, at, angle);
                                   x += size * um;
                                   ++filled;
                               }
                       };
                       for (size_t k = 0; k < row.size(); ++k) {
                           const int64_t gap = room / (int64_t)(row.size() + 1) / um * um;
                           fill(start + (int64_t)(k + 1) * gap + (int64_t)k * pad);
                           const auto [at, angle] = place(s, x, pad);
                           add(row[k].master, at, angle);
                           // The bond pad over the pad's pin (5 um in from its sides), outside the ring
                           const int64_t b0 = x + 5 * um;
                           Point bat = s.angle == 0 ? Point{b0, offset - bond} : s.angle == 90 ? Point{w - offset, b0} : s.angle == 180 ? Point{b0, h - offset} : Point{offset - bond, b0};
                           add("bondpad_70x70", bat, 0);
                           const layout::Rect r{bat.x, bat.y, bat.x + bond, bat.y + bond};
                           l.addLayoutShape(cell, view, layout::rectShape({126, 0}, r));
                           l.addLayoutShape(cell, view, layout::rectShape({126, 2}, r));
                           oadb::LayoutShape text;
                           text.layer = 126;
                           text.datatype = 25;
                           text.kind = "text";
                           text.points = {{bat.x + bond / 2, bat.y + bond / 2}};
                           text.text = row[k].name;
                           l.addLayoutShape(cell, view, text);
                           x += pad;
                           ++placed;
                       }
                       fill(end);
                   }
               });
               const int64_t core = offset + depth;
               return QJsonValue(QJsonObject{{"pads", placed}, {"fillers", filled},
                                             {"core", QJsonArray{(double)core, (double)core, (double)(w - core), (double)(h - core)}}});
           }});
    r.add({"layout_route_chip", "Routes a chip's top from its schematic: each pad's core pin (p2c, c2p) to the pin of the block its net reaches, on one track a net "
                                "in the channel between the block and the pads (Metal2 across it, Metal3 along it), and the supplies from the pad ring's "
                                "innermost rails onto the block's rings (the inner one over TopMetal2). For a chip of layout_pad_ring's pads around one "
                                "flow-built block whose pins face their pads. Returns the nets routed.",
           object({{"library", param("string", "the library")}, {"cell", param("string", "the chip's top cell: its schematic says what joins what")}},
                  {{"view", param("string", "the layout view (default layout)")}}),
           [](Context& c, const QJsonObject& a) {
               return QJsonValue(routeChip(c, a["library"].toString().toStdString(), a["cell"].toString().toStdString(),
                                           a.contains("view") ? a["view"].toString().toStdString() : "layout"));
           }});
    r.add({"layout_seal_ring", "Places IHP SG13G2's seal ring around a die: on each of its layers a ring stepped at the corners (four 4.2 um steps), "
                              "as IHP's sealring cell draws it: passivation from 25 um in, Activ, pSD, EdgeSeal and every metal from 32.2 um, 4.2 um wide, "
                              "contacts and vias as trenches inside them, Metal1 filler outside (in pieces of at most 5 um), and the die's boundary. Metal fill goes inside it. "
                              "Makes the cell sealring_<w>x<h> in the library and places it at the origin; returns its name.",
           object({{"library", param("string", "the library")}, {"cell", param("string", "the cell")},
                   {"die", param("array", "the die's width and height, [w, h] in microns (whole microns)")}},
                  {{"view", param("string", "the layout view (default layout)")}}),
           [](Context& c, const QJsonObject& a) {
               // ponytail: IHP's sealring at its own dimensions, read off its cell for a 1050 um die; another process needs its own table
               constexpr int64_t um = 1000, step = 4200;
               const std::string lib = a["library"].toString().toStdString(), cell = a["cell"].toString().toStdString();
               const std::string view = a.contains("view") ? a["view"].toString().toStdString() : "layout";
               oadb::Library& l = c.workspace.library(lib);
               if (l.viewType(cell, view) != "layout") throw std::invalid_argument(lib + "/" + cell + "/" + view + " is no layout view");
               const QJsonArray die = a["die"].toArray();
               if (die.size() != 2) throw std::invalid_argument("die is [w, h] in microns");
               for (const auto& v : die)
                   if (v.toDouble(-1) < 200 || v.toDouble() != std::floor(v.toDouble())) throw std::invalid_argument("the die's sides are whole microns, 200 or more");
               const int64_t w = (int64_t)die[0].toDouble() * um, h = (int64_t)die[1].toDouble() * um;
               // A stepped octagon d in from the die's edges, counter-clockwise from the south-west corner's top
               const std::vector<std::pair<int, int>> stair = {{0, 4}, {1, 4}, {1, 3}, {2, 3}, {2, 2}, {3, 2}, {3, 1}, {4, 1}, {4, 0}};
               struct Corners { std::vector<Point> sw, se, ne, nw; };
               const auto octagon = [&](int64_t d) {
                   Corners k;
                   for (const auto& [x, y] : stair) {
                       k.sw.push_back({d + x * step, d + y * step});
                       k.ne.push_back({w - d - x * step, h - d - y * step});
                   }
                   for (auto it = stair.rbegin(); it != stair.rend(); ++it) {
                       k.se.push_back({w - d - it->first * step, d + it->second * step});
                       k.nw.push_back({d + it->first * step, h - d - it->second * step});
                   }
                   return k;
               };
               // The ring between d and d + width as one polygon, as IHP's cell has it (Seal.m counts EdgeSeal's polygons): the outer edge
               // counter-clockwise from the south side's middle, a cut to the inner edge, and the inner edge clockwise
               const auto ring = [&](int64_t d, int64_t width) {
                   const Corners o = octagon(d), i = octagon(d + width);
                   const Point a{w / 2, d}, b{w / 2, d + width};
                   std::vector<Point> pts{a};
                   for (const auto* part : {&o.se, &o.ne, &o.nw, &o.sw}) pts.insert(pts.end(), part->begin(), part->end());
                   pts.push_back(a);
                   pts.push_back(b);
                   for (const auto* part : {&i.sw, &i.nw, &i.ne, &i.se}) pts.insert(pts.end(), part->rbegin(), part->rend());
                   pts.push_back(b);
                   return pts;
               };
               struct Layer { int layer, datatype; int64_t d, width; };
               std::vector<Layer> layers = {{9, 0, 25000, 4200}, {6, 0, 34200, 160}, {125, 0, 34200, 420}, {133, 0, 34200, 900}};
               for (int ly : {1, 14, 39, 8, 10, 30, 50, 67, 126, 134}) layers.push_back({ly, 0, 32200, 4200});
               for (int ly : {19, 29, 49, 66}) layers.push_back({ly, 0, 34200, 190});
               const std::string name = "sealring_" + std::to_string(w / um) + "x" + std::to_string(h / um);
               l.change("seal ring around " + cell, [&] {
                   const auto cells = l.cells();
                   if (std::find(cells.begin(), cells.end(), name) == cells.end()) {
                       l.createCell(name);
                       l.createView(name, "layout", "layout");
                       // A ring one step wide as IHP's cell draws it: a bar per side and five boxes per corner (Seal.m counts EdgeSeal's
                       // pieces with minimum coherence, and a polygon cut to its hole counts twice)
                       const auto boxes = [&](int64_t d) {
                           std::vector<layout::Rect> out;
                           const auto at = [&](int64_t x0, int64_t y0, int64_t x1, int64_t y1) {  // south-west corner frame, mirrored to all four
                               out.push_back({x0, y0, x1, y1});
                               out.push_back({w - x1, y0, w - x0, y1});
                               out.push_back({x0, h - y1, x1, h - y0});
                               out.push_back({w - x1, h - y1, w - x0, h - y0});
                           };
                           for (int k = 0; k < 5; ++k) at(d + k * step, d + (4 - k) * step, d + (k + 1) * step, d + std::min(5, 6 - k) * step);
                           out.push_back({d, d + 5 * step, d + step, h - d - 5 * step});
                           out.push_back({w - d - step, d + 5 * step, w - d, h - d - 5 * step});
                           out.push_back({d + 5 * step, d, w - d - 5 * step, d + step});
                           out.push_back({d + 5 * step, h - d - step, w - d - 5 * step, h - d});
                           return out;
                       };
                       for (const auto& ly : layers) {
                           if (ly.width == step) {
                               for (const auto& r : boxes(ly.d)) l.addLayoutShape(name, "layout", layout::rectShape({ly.layer, ly.datatype}, r));
                               continue;
                           }
                           oadb::LayoutShape sh;
                           sh.layer = ly.layer;
                           sh.datatype = ly.datatype;
                           sh.kind = "polygon";
                           sh.points = ring(ly.d, ly.width);
                           l.addLayoutShape(name, "layout", sh);
                       }
                       // Metal1 filler outside the ring, where IHP's cell has a strip, in pieces of 4.2 x 4.5 um 0.5 um apart (M1Fil.a2: at most
                       // 5 um either way)
                       const int64_t f0 = 25000, f1 = 29200, e = 53200;
                       for (int64_t x = e; x + 4500 <= w - e; x += 5000)
                           for (const layout::Rect& r : {layout::Rect{x, f0, x + 4500, f1}, layout::Rect{x, h - f1, x + 4500, h - f0}})
                               l.addLayoutShape(name, "layout", layout::rectShape({8, 22}, r));
                       for (int64_t y = e; y + 4500 <= h - e; y += 5000)
                           for (const layout::Rect& r : {layout::Rect{f0, y, f1, y + 4500}, layout::Rect{w - f1, y, w - f0, y + 4500}})
                               l.addLayoutShape(name, "layout", layout::rectShape({8, 22}, r));
                       l.addLayoutShape(name, "layout", layout::rectShape({39, 4}, {0, 0, w, h}));
                   }
                   oadb::LayoutInstance i;
                   i.cell = name;
                   l.addLayoutInstance(cell, view, i);
               });
               return QJsonValue(QString::fromStdString(name));
           }});
    r.add({"pdk_install", "Creates the libraries analogLib (ideal sources and passives) and a PDK's device library (sg13g2 for "
                          "IHP SG13G2: lv/hv MOS, MIM cap, poly resistors; asap7 / asap5 for the FinFET and nanowire PDKs: "
                          "nmos/pmos x slvt/lvt/rvt/sram), each cell a symbol with its spice template, defaults and model files; "
                          "returns the libraries and the PDK's model files for corners (and SG13G2's for Monte Carlo).",
           object({{"pdk", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"ihp-sg13g2", "asap7", "asap5"}}, {"description", "the PDK"}}},
                   {"root", param("string", "the PDK's directory: ihp-sg13g2 (holding libs.tech), asap7_pdk_r1p7 or asap5PDK_r0p4 "
                                            "(holding models/hspice)")},
                   {"osdi", param("string", "the compiled model the MOS devices load: psp103.osdi (SG13G2; the resistors load r3_cmc.osdi from "
                                            "beside it), bsimcmg107.osdi (ASAP7, ASAP5: tests/build_osdi.sh)")}}),
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
