// Library characterization (Liberate's job) on TreeSPICE: a standard cell's NLDM Liberty from its transistor netlist.
//
// The function comes from a DC truth table (every input combination, one copy of the cell each, one .op). Each timing arc
// (an output, an input that toggles it with the others held at a sensitizing state) is one transient: a copy of the cell per
// (input transition, output load) point, its input ramped up then down, so one run fills cell_rise, cell_fall, rise_transition
// and fall_transition. Thresholds are the common ones: delay at 50 %, transitions from 20 % to 80 % (derate 1). Inputs are driven
// through a predriver (two of the library's inverters, the second's load tuned to each transition), as characterization tools do.
// Input capacitance is the charge into the pin between the transition thresholds, at the fastest point.
//
// The indices come from a reference Liberty (the platform's, at the corner, by default) when it has the cell, and every value
// is compared with its own.
//
// ponytail: combinational cells only, NLDM timing only (no power, no CCS, no LVF), one sensitizing state per arc (the first;
// Liberate picks the worst or writes them all with `when`), a function written as a sum of minterms (correct, not minimal).
#include "cmd/registry.h"
#include "flow/rtl_flow.h"
#include "sim/netlist.h"
#include "sim/run.h"

#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <regex>
#include <optional>
#include <sstream>

namespace cmd {

namespace {

namespace fs = std::filesystem;
using acutesim::compute::orchestration::RtlFlowTools;

std::string s(const QJsonObject& a, const char* k) { return a[k].toString().toStdString(); }
QString q(const std::string& v) { return QString::fromStdString(v); }
std::string num(double v) {
    std::ostringstream o;
    o.precision(6);
    o << v;
    return o.str();
}

// ── A Liberty file, read as nested groups: enough to find a cell's pins and timing tables ───────────────────────────────
struct LibGroup {
    std::string type, arg;
    std::map<std::string, std::string> attrs;     // simple attributes, quotes removed
    std::map<std::string, std::string> complex;  // index_1 (...), values (...): the text between the parentheses
    std::vector<LibGroup> groups;
};

struct LibReader {
    const std::string& t;
    size_t i = 0;
    void space() {
        for (;;) {
            while (i < t.size() && (std::isspace((unsigned char)t[i]) || (t[i] == '\\' && i + 1 < t.size() && (t[i + 1] == '\n' || t[i + 1] == '\r')))) ++i;
            if (t.compare(i, 2, "/*") == 0) {
                const size_t e = t.find("*/", i + 2);
                i = e == std::string::npos ? t.size() : e + 2;
            } else if (t.compare(i, 2, "//") == 0) {
                while (i < t.size() && t[i] != '\n') ++i;
            } else {
                return;
            }
        }
    }
    std::string word() {
        space();
        const size_t b = i;
        while (i < t.size() && (std::isalnum((unsigned char)t[i]) || t[i] == '_' || t[i] == '.' || t[i] == '-' || t[i] == '!')) ++i;
        return t.substr(b, i - b);
    }
    std::string until(char end) {  // up to an unquoted end, which is consumed
        std::string out;
        bool quoted = false;
        for (; i < t.size(); ++i) {
            if (t[i] == '"') quoted = !quoted;
            else if (!quoted && t[i] == end) return ++i, out;
            if (t[i] == '\\' && i + 1 < t.size() && (t[i + 1] == '\n' || t[i + 1] == '\r')) continue;
            out += t[i];
        }
        throw std::runtime_error("Liberty: unterminated, expected '" + std::string(1, end) + "'");
    }
    static std::string unquote(std::string v) {
        v.erase(0, v.find_first_not_of(" \t\r\n"));
        v.erase(v.find_last_not_of(" \t\r\n") + 1);
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
        return v;
    }
    LibGroup group() {  // at "type (arg) {"
        LibGroup g;
        g.type = word();
        space();
        if (i >= t.size() || t[i] != '(') throw std::runtime_error("Liberty: expected ( after " + g.type);
        ++i;
        g.arg = unquote(until(')'));
        space();
        if (i >= t.size() || t[i] != '{') throw std::runtime_error("Liberty: expected { after " + g.type + " (" + g.arg + ")");
        ++i;
        for (;;) {
            space();
            if (i >= t.size()) throw std::runtime_error("Liberty: group " + g.type + " (" + g.arg + ") is not closed");
            if (t[i] == '}') return ++i, g;
            const size_t at = i;
            const std::string name = word();
            if (name.empty()) throw std::runtime_error("Liberty: unexpected '" + std::string(1, t[i]) + "' in " + g.type + " (" + g.arg + ")");
            space();
            if (t[i] == ':') {
                ++i;
                g.attrs[name] = unquote(until(';'));
            } else if (t[i] == '(') {
                ++i;
                const std::string args = until(')');
                space();
                if (i < t.size() && t[i] == '{') {
                    i = at;
                    g.groups.push_back(group());
                } else {
                    if (i < t.size() && t[i] == ';') ++i;
                    g.complex[name] = args;
                }
            } else {
                throw std::runtime_error("Liberty: expected : or ( after " + name);
            }
        }
    }
};

std::vector<double> numbers(const std::string& text) {
    std::vector<double> out;
    std::string cleaned = text;
    for (char& ch : cleaned)
        if (ch == '"' || ch == ',' || ch == '\\') ch = ' ';
    std::istringstream in(cleaned);
    for (double v; in >> v;) out.push_back(v);
    return out;
}

// The cell's group in a Liberty file, or nothing when it has no such cell
std::optional<LibGroup> libertyCell(const std::string& file, const std::string& cell) {
    std::ifstream in(file);
    if (!in) throw std::invalid_argument("cannot read Liberty " + file);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::smatch m;
    if (!std::regex_search(text, m, std::regex("\\bcell\\s*\\(\\s*\"?" + cell + "\"?\\s*\\)\\s*\\{"))) return std::nullopt;
    LibReader r{text, size_t(m.position(0))};
    return r.group();
}

// ── Simulation results ──────────────────────────────────────────────────────────────────────────────────────────────────
struct Wave {
    const std::vector<std::complex<double>>* t;
    const std::vector<std::complex<double>>* v;
    // the first time from `from` on that v crosses level going up (or down); NaN when it does not
    double cross(double level, bool up, double from) const {
        for (size_t k = 1; k < t->size(); ++k) {
            const double t0 = (*t)[k - 1].real(), t1 = (*t)[k].real();
            if (t1 < from) continue;
            const double a = (*v)[k - 1].real(), b = (*v)[k].real();
            if (up ? (a < level && b >= level) : (a > level && b <= level)) return t0 + (level - a) / (b - a) * (t1 - t0);
        }
        return std::nan("");
    }
    double at(double time) const {
        const auto it = std::lower_bound(t->begin(), t->end(), time, [](const std::complex<double>& x, double y) { return x.real() < y; });
        if (it == t->end()) return v->back().real();
        return (*v)[size_t(it - t->begin())].real();
    }
};

const sim::Plot& plotNamed(const sim::Run& r, const char* part) {
    for (const auto& p : r.plots)
        if (p.name.find(part) != std::string::npos) return p;
    throw std::runtime_error(std::string("the simulation wrote no ") + part + " results");
}
const std::vector<std::complex<double>>& vec(const sim::Plot& p, const std::string& name) {
    for (size_t k = 0; k < p.vectors.size(); ++k)
        if (p.vectors[k] == name) return p.data[k];
    throw std::runtime_error("the simulation wrote no " + name);
}

std::string lower(std::string v) {
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char ch) { return std::tolower(ch); });
    return v;
}

struct Arc {
    std::string output, input, sense;       // sense: positive_unate or negative_unate
    std::map<std::string, bool> side;       // the other inputs while this one toggles
    std::map<std::string, std::vector<std::vector<double>>> tables;  // cell_rise, cell_fall, rise_transition, fall_transition [slew][load], ns
};

}  // namespace

void addCharCommands(Registry& r) {
    r.add({"lib_characterize",
           "Characterizes a combinational standard cell on TreeSPICE (Liberate's job): its function from a DC truth table, then each timing arc's "
           "delay and output transition over input transitions and output loads, as an NLDM Liberty file. The indices are the reference "
           "Liberty's for the cell (the platform's at the corner, by default), and every value is compared with it. Returns the file, the "
           "function, the arcs with their tables and their differences from the reference, and the pins' capacitance.",
           object({{"library", param("string", "the cell's library (pdk_install_cells)")}, {"cell", param("string", "a combinational cell with a spice property")}},
                  {{"corner", param("string", "the platform's transistor-level corner (slow, typ, fast); default typ")},
                   {"reference", param("string", "a Liberty file to take the indices from and compare with (default: the platform's at the corner)")},
                   {"slews", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "number"}}}, {"description", "input transitions, ns (20-80 %), instead of the reference's"}}},
                   {"loads", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "number"}}}, {"description", "output loads, pF, instead of the reference's"}}},
                   {"predriver", param("string", "the inverter of the library that drives inputs, as characterization tools do (default its *inv_4); ramp: a linear ramp")},
                   {"timeout_s", param("integer", "per simulation (default 600)")}}),
           [](Context& c, const QJsonObject& a) {
               const auto started = std::chrono::steady_clock::now();
               const std::string lib = s(a, "library"), cell = s(a, "cell");
               oadb::Library& l = c.workspace.library(lib);
               const auto spiceText = l.cellProp(cell, "spice");
               if (!spiceText) throw std::invalid_argument(lib + "/" + cell + " has no spice property: nothing to simulate");
               const QJsonObject spice = QJsonDocument::fromJson(QByteArray::fromStdString("[" + *spiceText + "]")).array()[0].toObject();
               const flow::Platform& pdk = platformOf(l);

               // The corner: its MOS section, supply and temperature
               const std::string cornerName = a.contains("corner") ? s(a, "corner") : "typ";
               const flow::SpiceCorner* corner = nullptr;
               std::string known;
               for (const auto& k : pdk.spiceCorners) {
                   known += (known.empty() ? "" : ", ") + k.name;
                   if (k.name == cornerName) corner = &k;
               }
               if (!corner) throw std::invalid_argument(pdk.name + " has no transistor-level corner '" + cornerName + "' (it has " + (known.empty() ? "none" : known) + ")");
               const double vdd = corner->volts;
               std::string cards;
               const QJsonObject globals = spice["globals"].toObject();
               for (auto it = globals.begin(); it != globals.end(); ++it) cards += ".param " + it.key().toStdString() + "=" + it.value().toVariant().toString().toStdString() + "\n";
               for (const auto& m : spice["models"].toArray()) {
                   std::string section = m.toObject()["section"].toString().toStdString();
                   if (section.rfind("mos_", 0) == 0) section = corner->mosSection;
                   cards += sim::modelCard({m.toObject()["file"].toString().toStdString(), section}) + "\n";
               }
               cards += ".temp " + num(corner->celsius) + "\n";

               // Its pins: inputs, outputs and the supplies, by the symbol
               std::vector<std::string> inputs, outputs;
               std::string power, ground;
               static const std::regex powerName("^(vdd|vpwr|vcc)", std::regex::icase), groundName("^(vss|vgnd|gnd)", std::regex::icase);
               for (const auto& p : l.pins(cell, "symbol")) {
                   if (p.direction == "input") inputs.push_back(p.name);
                   else if (p.direction == "output") outputs.push_back(p.name);
                   else if (std::regex_search(p.name, powerName)) power = p.name;
                   else if (std::regex_search(p.name, groundName)) ground = p.name;
                   else throw std::invalid_argument(cell + "'s pin " + p.name + " is " + p.direction + ": only inputs, outputs and supplies are characterized");
               }
               if (inputs.empty() || outputs.empty() || power.empty() || ground.empty())
                   throw std::invalid_argument(cell + " needs inputs, outputs and a power and ground pin (by name: vdd/vpwr, vss/vgnd)");
               if (inputs.size() > 8) throw std::invalid_argument(cell + " has " + std::to_string(inputs.size()) + " inputs: at most 8");
               const auto instance = [&](const std::string& name, const std::map<std::string, std::string>& nets) {
                   oadb::Instance i;
                   i.name = name;
                   i.lib = lib;
                   i.cell = cell;
                   i.view = "symbol";
                   std::map<std::string, std::string> all = nets;
                   all[power] = "vdd";
                   all[ground] = "0";
                   return sim::primitiveLine(c.workspace, i, all) + "\n";
               };

               // The predriver: an inverter of the same library (default its *inv_4), or "ramp"
               std::string predriver = a.contains("predriver") ? s(a, "predriver") : "";
               if (!a.contains("predriver"))
                   for (const auto& name : l.cells())
                       if (std::regex_search(name, std::regex("inv_4$"))) predriver = name;
               if (predriver.empty()) throw std::invalid_argument(lib + " has no cell named *inv_4 to drive inputs: give predriver (an inverter of " + lib + ", or ramp)");
               if (predriver == "ramp") predriver.clear();
               std::string predriverIn, predriverOut, predriverPower, predriverGround;
               if (!predriver.empty()) {
                   for (const auto& p : l.pins(predriver, "symbol")) {
                       if (p.direction == "input") predriverIn = p.name;
                       else if (p.direction == "output") predriverOut = p.name;
                       else if (std::regex_search(p.name, powerName)) predriverPower = p.name;
                       else if (std::regex_search(p.name, groundName)) predriverGround = p.name;
                   }
                   if (predriverIn.empty() || predriverOut.empty() || predriverPower.empty() || predriverGround.empty() || !l.cellProp(predriver, "spice"))
                       throw std::invalid_argument("the predriver " + predriver + " is not an inverter of " + lib + " with a spice property");
               }

               // The reference: indices, and values to compare with
               std::string reference;
               if (a.contains("reference")) {
                   reference = s(a, "reference");
               } else {
                   const RtlFlowTools tools = RtlFlowTools::detect();
                   const auto& files = flow::libertyFor(pdk, cornerName);
                   if (!tools.flowDir.empty() && !files.empty()) reference = flow::platformFile(pdk, tools.flowDir, files.front());
               }
               std::optional<LibGroup> ref;
               if (!reference.empty()) ref = libertyCell(reference, cell);
               if (ref)
                   for (const auto& g : ref->groups)
                       if (g.type == "ff" || g.type == "latch" || g.type == "statetable")
                           throw std::invalid_argument(cell + " is sequential (its Liberty has " + g.type + "): only combinational cells are characterized yet");
               std::vector<double> slews, loads;
               for (const auto& v : a["slews"].toArray()) slews.push_back(v.toDouble());
               for (const auto& v : a["loads"].toArray()) loads.push_back(v.toDouble());
               if (slews.empty() != loads.empty()) throw std::invalid_argument("give both slews and loads, or neither");
               if (slews.empty() && ref)
                   for (const auto& pin : ref->groups)
                       for (const auto& t : pin.groups)
                           if (t.type == "timing" && slews.empty())
                               for (const auto& tab : t.groups)
                                   if (tab.type == "cell_rise" && tab.complex.count("index_1") && tab.complex.count("index_2")) {
                                       slews = numbers(tab.complex.at("index_1"));
                                       loads = numbers(tab.complex.at("index_2"));
                                   }
               if (slews.empty())
                   throw std::invalid_argument("no indices: " + (reference.empty() ? std::string("no reference Liberty") : reference + " has no timing table for " + cell) +
                                               "; give slews and loads");
               for (double v : slews)
                   if (!(v > 0)) throw std::invalid_argument("slews must be > 0 ns");
               for (double v : loads)
                   if (!(v >= 0)) throw std::invalid_argument("loads must be >= 0 pF");

               fs::path dir = fs::absolute(fs::path(c.workspace.file()).parent_path()) / "characterization" / (lib + "_" + cell);
               fs::create_directories(dir);
               int n = 0;
               while (!fs::create_directory(dir / ("run." + std::to_string(++n)))) {}
               dir /= "run." + std::to_string(n);
               const auto timeout = std::chrono::seconds(a["timeout_s"].toInt(600));

               // 1. The function: every input combination as a copy of the cell, inputs tied to the rails
               const size_t rows = size_t(1) << inputs.size();
               std::string deck = "* " + cell + ": truth table\nVVDD vdd 0 " + num(vdd) + "\n" + cards;
               for (size_t r = 0; r < rows; ++r) {
                   std::map<std::string, std::string> nets;
                   for (size_t k = 0; k < inputs.size(); ++k) nets[inputs[k]] = (r >> k & 1) ? "vdd" : "0";
                   for (const auto& o : outputs) nets[o] = "o" + std::to_string(r) + "_" + lower(o);
                   deck += instance("XT" + std::to_string(r), nets);
               }
               fs::create_directories(dir / "truth");
               const sim::Run truthRun = sim::simulate(c.simulator, (dir / "truth").string(), deck + ".op\n.end\n", timeout);
               const sim::Plot& op = plotNamed(truthRun, "Operating Point");
               std::map<std::string, std::vector<bool>> truth;  // output -> its value per row
               for (const auto& o : outputs)
                   for (size_t r = 0; r < rows; ++r) {
                       const double v = vec(op, "v(o" + std::to_string(r) + "_" + lower(o) + ")")[0].real();
                       if (v > 0.1 * vdd && v < 0.9 * vdd) {
                           std::string state;
                           for (size_t k = 0; k < inputs.size(); ++k) state += " " + inputs[k] + "=" + ((r >> k & 1) ? "1" : "0");
                           throw std::runtime_error(cell + "'s " + o + " is at " + num(v) + " V with" + state + ": not a static CMOS function");
                       }
                       truth[o].push_back(v > vdd / 2);
                   }
               QJsonObject functions;
               std::map<std::string, std::string> functionOf;
               for (const auto& o : outputs) {
                   const auto& tt = truth[o];
                   const size_t ones = size_t(std::count(tt.begin(), tt.end(), true));
                   std::string f;
                   if (ones == 0 || ones == rows) {
                       f = ones ? "1" : "0";
                   } else {
                       const bool on = ones <= rows / 2;  // the smaller side, as a sum of its minterms
                       std::vector<std::string> terms;
                       for (size_t r = 0; r < rows; ++r) {
                           if (tt[r] != on) continue;
                           std::string term;
                           for (size_t k = 0; k < inputs.size(); ++k) term += (term.empty() ? "" : "*") + std::string((r >> k & 1) ? "" : "!") + inputs[k];
                           terms.push_back(term);
                       }
                       for (const auto& t : terms) f += (f.empty() ? "" : "+") + (terms.size() > 1 && inputs.size() > 1 ? "(" + t + ")" : t);
                       if (!on) f = inputs.size() == 1 && terms.size() == 1 ? "!" + f : "!(" + f + ")";
                   }
                   functionOf[o] = f;
                   functions[q(o)] = q(f);
               }

               // 2. The arcs: an input that toggles an output, the others at the first state that lets it
               std::vector<Arc> arcs;
               for (const auto& o : outputs)
                   for (size_t k = 0; k < inputs.size(); ++k) {
                       std::map<std::string, Arc> bySense;
                       for (size_t r = 0; r < rows; ++r) {
                           if (r >> k & 1) continue;
                           const bool lo = truth[o][r], hi = truth[o][r | (size_t(1) << k)];
                           if (lo == hi) continue;
                           const std::string sense = hi ? "positive_unate" : "negative_unate";
                           if (bySense.count(sense)) continue;
                           Arc arc{o, inputs[k], sense, {}, {}};
                           for (size_t j = 0; j < inputs.size(); ++j)
                               if (j != k) arc.side[inputs[j]] = r >> j & 1;
                           bySense[sense] = arc;
                       }
                       for (auto& [sense, arc] : bySense) arcs.push_back(arc);
                   }
               if (arcs.empty()) throw std::runtime_error(cell + "'s outputs follow none of its inputs");

               // 3. The input edges, one per (transition, direction): a predriver's output (two of the library's inverters, the second loaded to give
               // the transition asked for), as characterization tools drive inputs. A linear ramp's sharp end moves delays by up to a fifth where the
               // input is as slow as the output (IHP's tables are a predriver's). Each arc's deck holds the predrivers, and every copy of the cell
               // takes its edge through an ideal buffer (a PWL of the edge instead puts thousands of breakpoints in the run). "ramp": linear.
               const double maxSlew = *std::max_element(slews.begin(), slews.end()), minSlew = *std::min_element(slews.begin(), slews.end());
               const double t1 = 0.1;  // ns: when each edge starts
               const std::string lo = "0", hi = num(vdd);
               std::map<std::pair<size_t, bool>, double> loadOf;  // (slew, up) -> the predriver's load, pF
               int simulations = 1;
               const auto inv = [&](const std::string& name, const std::string& in, const std::string& out) {
                   oadb::Instance i;
                   i.name = name;
                   i.lib = lib;
                   i.cell = predriver;
                   i.view = "symbol";
                   return sim::primitiveLine(c.workspace, i, {{predriverIn, in}, {predriverOut, out}, {predriverPower, "vdd"}, {predriverGround, "0"}}) + "\n";
               };
               // an edge at node p<id>: the predriver's, or a ramp
               const auto source = [&](const std::string& id, size_t si, bool up, double cp) {
                   if (predriver.empty())
                       return "VS" + id + " p" + id + " 0 PWL(0 " + (up ? lo : hi) + " " + num(t1) + "n " + (up ? lo : hi) + " " + num(t1 + slews[si] / 0.6) + "n " + (up ? hi : lo) + ")\n";
                   return "VS" + id + " s" + id + " 0 PWL(0 " + (up ? lo : hi) + " " + num(t1) + "n " + (up ? lo : hi) + " " + num(t1 + 0.01) + "n " + (up ? hi : lo) + ")\n" +
                          inv("XPA" + id, "s" + id, "m" + id) + inv("XPB" + id, "m" + id, "p" + id) + "CP" + id + " p" + id + " 0 " + num(cp) + "p\n";
               };
               if (!predriver.empty()) {
                   struct Cal {
                       size_t si;
                       bool up;
                       double cp;  // the predriver's load, pF
                       std::string id;
                   };
                   const double calStop = t1 + 4 * maxSlew / 0.6 + 1.0;
                   const auto calDeck = [&](const std::vector<Cal>& cs) {
                       std::string d = "* " + cell + ": the predriver's edges\nVVDD vdd 0 " + num(vdd) + "\n" + cards, save = ".save";
                       for (const Cal& k : cs) {
                           d += source(k.id, k.si, k.up, k.cp);
                           save += " v(p" + k.id + ")";
                       }
                       return d + save + "\n.tran 1p " + num(calStop) + "n 0 " + num(std::max(0.5, std::min(10.0, minSlew / 0.6 * 100))) + "p\n.end\n";
                   };
                   const auto slewOf = [&](const Wave& w, bool up) {
                       return (w.cross((up ? 0.8 : 0.2) * vdd, up, t1 * 1e-9) - w.cross((up ? 0.2 : 0.8) * vdd, up, t1 * 1e-9)) * 1e9;
                   };
                   const auto calRun = [&](const std::vector<Cal>& cs, const std::string& name) {
                       const fs::path d = dir / name;
                       fs::create_directories(d);
                       ++simulations;
                       return sim::simulate(c.simulator, d.string(), calDeck(cs), timeout, nullptr, 1);
                   };
                   // its transition against its load: two points of a line, rising and falling, which each edge then corrects
                   const double c1 = 0.2;
                   std::map<bool, std::pair<double, double>> line;  // up -> (transition at no load, ns per pF)
                   {
                       const sim::Run run = calRun({{0, true, 0, "0r"}, {0, true, c1, "1r"}, {0, false, 0, "0f"}, {0, false, c1, "1f"}}, "predriver");
                       const sim::Plot& tr = plotNamed(run, "Transient");
                       const auto& time = vec(tr, "time");
                       for (const bool up : {true, false}) {
                           const std::string e = up ? "r" : "f";
                           const double s0 = slewOf({&time, &vec(tr, "v(p0" + e + ")")}, up), s1 = slewOf({&time, &vec(tr, "v(p1" + e + ")")}, up);
                           if (!(s0 > 0) || !(s1 > s0)) throw std::runtime_error("the predriver " + predriver + " gave no usable edge (" + run.dir + ")");
                           if (minSlew < s0 * 1.01)
                               throw std::invalid_argument("the predriver " + predriver + " cannot make an edge of " + num(minSlew) + " ns (its fastest is " + num(s0) + " ns): choose a stronger one");
                           line[up] = {s0, (s1 - s0) / c1};
                       }
                   }
                   std::vector<Cal> cs;
                   for (size_t si = 0; si < slews.size(); ++si)
                       for (const bool up : {true, false})
                           cs.push_back({si, up, (slews[si] - line[up].first) / line[up].second, std::string(up ? "r" : "f") + std::to_string(si)});
                   for (int pass = 0;; ++pass) {  // each edge's load moved until its transition is the index's within 0.2 %
                       const sim::Run run = calRun(cs, "predriver." + std::to_string(pass + 1));
                       const sim::Plot& tr = plotNamed(run, "Transient");
                       const auto& time = vec(tr, "time");
                       bool off = false;
                       for (Cal& k : cs) {
                           const Wave w{&time, &vec(tr, "v(p" + k.id + ")")};
                           const double got = slewOf(w, k.up), target = slews[k.si];
                           if (std::abs(got / target - 1) > 0.002) {
                               off = true;
                               const double s0 = line[k.up].first, slope = k.cp > 0 && got > s0 ? (got - s0) / k.cp : line[k.up].second;
                               k.cp = std::max(0.0, k.cp + (target - got) / slope);
                           }
                           loadOf[{k.si, k.up}] = k.cp;
                       }
                       if (!off) break;
                       if (pass == 5) throw std::runtime_error(cell + ": the predriver's edges did not come within 0.2 % of the transitions asked for (" + run.dir + ")");
                   }
               }
               const double stop = t1 + 4 * maxSlew / 0.6 + 10.0;  // ns: the edge, and time to settle

               // 4. Each arc: a transient, a copy of the cell per (transition, load, input edge), its input on its edge through an ideal buffer
               struct Copy {
                   size_t si, li;
                   bool up;
                   std::string id;
               };
               const auto copyDeck = [&](const Arc& arc, const std::vector<Copy>& copies) {
                   std::string d = "* " + cell + ": " + arc.input + " to " + arc.output + "\nVVDD vdd 0 " + num(vdd) + "\n" + cards, save = ".save";
                   for (size_t si = 0; si < slews.size(); ++si)
                       for (const bool up : {true, false}) d += source(std::string(up ? "r" : "f") + std::to_string(si), si, up, loadOf[{si, up}]);
                   for (const Copy& k : copies) {
                       // the edge, buffered; the cell's input behind a 0 V source, whose current gives its capacitance
                       d += "EB" + k.id + " b" + k.id + " 0 p" + (k.up ? "r" : "f") + std::to_string(k.si) + " 0 1\nVIN" + k.id + " b" + k.id + " a" + k.id + " 0\n";
                       std::map<std::string, std::string> nets;
                       for (const auto& [pin, v] : arc.side) nets[pin] = v ? "vdd" : "0";
                       nets[arc.input] = "a" + k.id;
                       for (const auto& o : outputs) nets[o] = (o == arc.output ? "y" : "z_" + lower(o) + "_") + k.id;
                       d += instance("XC" + k.id, nets) + "CL" + k.id + " y" + k.id + " 0 " + num(loads[k.li]) + "p\n";
                       save += " v(a" + k.id + ") v(y" + k.id + ") i(vin" + k.id + ")";
                   }
                   return d + save + "\n.tran 1p " + num(stop) + "n 0 " + num(std::max(0.5, std::min(10.0, minSlew / 0.6 * 100))) + "p\n.end\n";
               };
               QJsonArray arcsOut;
               std::map<std::string, std::pair<double, double>> capacitance;  // input -> (rise, fall), pF
               double worst = 0;
               int index = 0;
               std::vector<Copy> copies;
               for (const bool up : {true, false})
                   for (size_t si = 0; si < slews.size(); ++si)
                       for (size_t li = 0; li < loads.size(); ++li) copies.push_back({si, li, up, std::string(up ? "r" : "f") + std::to_string(si) + "_" + std::to_string(li)});
               // every arc at once, each on one thread: a few hundred transistors gain nothing from more, and on a busy machine lose half
               std::vector<std::future<sim::Run>> pending;
               for (size_t k = 0; k < arcs.size(); ++k) {
                   const fs::path runDir = dir / ("arc" + std::to_string(k + 1));
                   fs::create_directories(runDir);
                   pending.push_back(std::async(std::launch::async, [simulator = c.simulator, d = runDir.string(), text = copyDeck(arcs[k], copies), timeout] {
                       return sim::simulate(simulator, d, text, timeout, nullptr, 1);
                   }));
                   ++simulations;
               }
               for (Arc& arc : arcs) {
                   const bool positive = arc.sense == "positive_unate";
                   const fs::path runDir = dir / ("arc" + std::to_string(index + 1));
                   const std::optional<sim::Run> run = pending[size_t(index++)].get();
                   const sim::Plot& tr = plotNamed(*run, "Transient");
                   const auto& time = vec(tr, "time");
                   for (const char* t : {"cell_rise", "cell_fall", "rise_transition", "fall_transition"})
                       arc.tables[t].assign(slews.size(), std::vector<double>(loads.size(), 0));
                   for (const Copy& k : copies) {
                       const Wave in{&time, &vec(tr, "v(a" + k.id + ")")}, out{&time, &vec(tr, "v(y" + k.id + ")")};
                       const double from = t1 * 1e-9, end = stop * 1e-9;
                       const bool outUp = k.up == positive;
                       const double tin = in.cross(vdd / 2, k.up, from), tout = out.cross(vdd / 2, outUp, from);
                       const double t20 = out.cross((outUp ? 0.2 : 0.8) * vdd, outUp, from), t80 = out.cross((outUp ? 0.8 : 0.2) * vdd, outUp, from);
                       if (std::isnan(tin) || std::isnan(tout) || std::isnan(t20) || std::isnan(t80) || std::abs(out.at(end * (1 - 1e-6)) - (outUp ? vdd : 0)) > 0.05 * vdd)
                           throw std::runtime_error(cell + ": " + arc.input + " to " + arc.output + " at " + num(slews[k.si]) + " ns, " + num(loads[k.li]) +
                                                    " pF: the output did not switch and settle by " + num(stop) + " ns (" + runDir.string() + ")");
                       arc.tables[outUp ? "cell_rise" : "cell_fall"][k.si][k.li] = (tout - tin) * 1e9;
                       arc.tables[outUp ? "rise_transition" : "fall_transition"][k.si][k.li] = (t80 - t20) * 1e9;
                   }
                   // the input's capacitance: the charge into it while it swings between the transition thresholds (20 to 80 %), over that
                   // swing, at the fastest point (IHP's within 2 %; the whole edge's charge adds the output's Miller step, 30 % more)
                   if (!capacitance.count(arc.input)) {
                       const auto charge = [&](const std::string& id) {
                           const auto& i = vec(tr, "i(vin" + id + ")");
                           const auto& v = vec(tr, "v(a" + id + ")");
                           double q = 0;
                           for (size_t k = 1; k < time.size(); ++k) {
                               const double mid = 0.5 * (v[k].real() + v[k - 1].real()) / vdd;
                               if (mid >= 0.2 && mid <= 0.8) q += 0.5 * (i[k].real() + i[k - 1].real()) * (time[k].real() - time[k - 1].real());
                           }
                           return std::abs(q) / (0.6 * vdd) * 1e12;
                       };
                       capacitance[arc.input] = {charge("r0_0"), charge("f0_0")};
                   }

                   QJsonObject tables, compare;
                   for (const auto& [name, t] : arc.tables) {
                       QJsonArray rowsOut;
                       for (const auto& row : t) {
                           QJsonArray r2;
                           for (double v : row) r2.append(v);
                           rowsOut.append(r2);
                       }
                       tables[q(name)] = rowsOut;
                   }
                   // against the reference's own table for this arc
                   if (ref) {
                       const LibGroup* match = nullptr;
                       for (const auto& pin : ref->groups)
                           if (pin.type == "pin" && pin.arg == arc.output)
                               for (const auto& t : pin.groups)
                                   if (!match && t.type == "timing" && t.attrs.count("related_pin") && t.attrs.at("related_pin") == arc.input &&
                                       (!t.attrs.count("timing_sense") || t.attrs.at("timing_sense") == arc.sense))
                                       match = &t;
                       for (const auto& [name, t] : arc.tables) {
                           const LibGroup* theirs = nullptr;
                           if (match)
                               for (const auto& g : match->groups)
                                   if (g.type == name) theirs = &g;
                           if (!theirs || !theirs->complex.count("values")) {
                               compare[q(name)] = "the reference has no " + q(name) + " for this arc";
                               continue;
                           }
                           if (numbers(theirs->complex.at("index_1")) != slews || numbers(theirs->complex.at("index_2")) != loads) {
                               compare[q(name)] = "the reference's indices differ: not compared";
                               continue;
                           }
                           const std::vector<double> values = numbers(theirs->complex.at("values"));
                           if (values.size() != slews.size() * loads.size()) throw std::runtime_error(reference + ": " + cell + " " + name + " has " + std::to_string(values.size()) + " values");
                           double maxRel = 0, maxAbs = 0, sumRel = 0;
                           QJsonObject at;
                           for (size_t si = 0; si < slews.size(); ++si)
                               for (size_t li = 0; li < loads.size(); ++li) {
                                   const double ours = t[si][li], other = values[si * loads.size() + li], rel = std::abs(ours - other) / std::abs(other);
                                   sumRel += rel;
                                   maxAbs = std::max(maxAbs, std::abs(ours - other));
                                   if (rel > maxRel) {
                                       maxRel = rel;
                                       at = QJsonObject{{"slew_ns", slews[si]}, {"load_pf", loads[li]}, {"ours_ns", ours}, {"reference_ns", other}};
                                   }
                               }
                           worst = std::max(worst, maxRel);
                           compare[q(name)] = QJsonObject{{"max_relative", maxRel}, {"mean_relative", sumRel / double(slews.size() * loads.size())},
                                                          {"max_absolute_ns", maxAbs}, {"worst", at}};
                       }
                   }
                   QJsonObject side;
                   for (const auto& [pin, v] : arc.side) side[q(pin)] = v;
                   QJsonObject entry{{"output", q(arc.output)}, {"input", q(arc.input)}, {"sense", q(arc.sense)}, {"side", side}, {"tables", tables}};
                   if (ref) entry["compare"] = compare;
                   arcsOut.append(entry);
               }

               // 4. The Liberty
               const auto row = [](const std::vector<double>& v) {
                   std::string out;
                   for (double x : v) out += (out.empty() ? "" : ", ") + num(x);
                   return "\"" + out + "\"";
               };
               std::ostringstream lib_;
               lib_ << "/* " << cell << " characterized by OpenVLSI on TreeSPICE: " << pdk.name << " " << cornerName << ", " << num(vdd) << " V, " << num(corner->celsius)
                    << " C */\nlibrary (" << cell << "_" << cornerName << ") {\n  delay_model : table_lookup;\n  time_unit : \"1ns\";\n  voltage_unit : \"1V\";\n"
                    << "  current_unit : \"1mA\";\n  capacitive_load_unit (1, pf);\n  nom_process : 1;\n  nom_voltage : " << num(vdd) << ";\n  nom_temperature : "
                    << num(corner->celsius) << ";\n  input_threshold_pct_rise : 50;\n  input_threshold_pct_fall : 50;\n  output_threshold_pct_rise : 50;\n"
                    << "  output_threshold_pct_fall : 50;\n  slew_lower_threshold_pct_rise : 20;\n  slew_lower_threshold_pct_fall : 20;\n"
                    << "  slew_upper_threshold_pct_rise : 80;\n  slew_upper_threshold_pct_fall : 80;\n  slew_derate_from_library : 1;\n"
                    << "  lu_table_template (delay_template) {\n    variable_1 : input_net_transition;\n    variable_2 : total_output_net_capacitance;\n    index_1 ("
                    << row(slews) << ");\n    index_2 (" << row(loads) << ");\n  }\n  cell (" << cell << ") {\n";
               if (ref && ref->attrs.count("area")) lib_ << "    area : " << ref->attrs.at("area") << ";\n";
               QJsonObject pinsOut;
               for (const auto& in : inputs) {
                   const auto it = capacitance.find(in);
                   if (it == capacitance.end()) throw std::runtime_error(cell + "'s input " + in + " affects no output");
                   const double cap = (it->second.first + it->second.second) / 2;
                   lib_ << "    pin (" << in << ") {\n      direction : input;\n      capacitance : " << num(cap) << ";\n      rise_capacitance : " << num(it->second.first)
                        << ";\n      fall_capacitance : " << num(it->second.second) << ";\n    }\n";
                   QJsonObject p{{"capacitance_pf", cap}, {"rise_capacitance_pf", it->second.first}, {"fall_capacitance_pf", it->second.second}};
                   if (ref)
                       for (const auto& g : ref->groups)
                           if (g.type == "pin" && g.arg == in && g.attrs.count("capacitance")) p["reference_pf"] = std::stod(g.attrs.at("capacitance"));
                   pinsOut[q(in)] = p;
               }
               for (const auto& o : outputs) {
                   lib_ << "    pin (" << o << ") {\n      direction : output;\n      function : \"" << functionOf[o] << "\";\n      max_capacitance : "
                        << num(*std::max_element(loads.begin(), loads.end())) << ";\n";
                   for (const Arc& arc : arcs) {
                       if (arc.output != o) continue;
                       lib_ << "      timing () {\n        related_pin : \"" << arc.input << "\";\n        timing_sense : " << arc.sense << ";\n        timing_type : combinational;\n";
                       // two senses on one pin (an XOR): each holds for its side inputs
                       if (std::count_if(arcs.begin(), arcs.end(), [&](const Arc& x) { return x.output == o && x.input == arc.input; }) > 1) {
                           std::string when;
                           for (const auto& [pin, v] : arc.side) when += (when.empty() ? "" : "*") + std::string(v ? "" : "!") + pin;
                           if (!when.empty()) lib_ << "        when : \"" << when << "\";\n        sdf_cond : \"" << when << "\";\n";
                       }
                       for (const char* name : {"cell_rise", "rise_transition", "cell_fall", "fall_transition"}) {
                           lib_ << "        " << name << " (delay_template) {\n          values ( \\\n";
                           const auto& t = arc.tables.at(name);
                           for (size_t si = 0; si < t.size(); ++si) lib_ << "            " << row(t[si]) << (si + 1 < t.size() ? ", \\\n" : " \\\n");
                           lib_ << "          );\n        }\n";
                       }
                       lib_ << "      }\n";
                   }
                   lib_ << "    }\n";
               }
               lib_ << "    pg_pin (" << power << ") {\n      pg_type : primary_power;\n      voltage_name : \"" << power << "\";\n    }\n    pg_pin (" << ground
                    << ") {\n      pg_type : primary_ground;\n      voltage_name : \"" << ground << "\";\n    }\n  }\n}\n";
               const fs::path libFile = dir / (cell + ".lib");
               std::ofstream(libFile) << lib_.str();

               std::string runName = lib + "_" + cell + "/run." + std::to_string(n);
               QJsonObject result{{"run", q(runName)}, {"liberty", q(libFile.string())}, {"function", functions}, {"arcs", arcsOut}, {"pins", pinsOut},
                                  {"corner", q(cornerName)}, {"supply_v", vdd}, {"temperature_c", corner->celsius}, {"points", double(slews.size() * loads.size())}, {"simulations", simulations},
                                  {"predriver", predriver.empty() ? QString("ramp") : q(predriver)},
                                  {"seconds", std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count()}};
               if (ref) {
                   result["reference"] = q(reference);
                   result["worst_relative"] = worst;
               }
               std::ofstream(dir / "result.json") << QJsonDocument(result).toJson().toStdString();
               return QJsonValue(result);
           }});
}

}  // namespace cmd
