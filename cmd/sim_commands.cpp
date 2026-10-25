// Netlisting and simulation commands: a schematic (or config) to SPICE, run on TreeSPICE, results read back
#include "cmd/registry.h"
#include "oadb/oadb.h"
#include "sim/calc.h"
#include "sim/netlist.h"
#include "sim/run.h"

#include <algorithm>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>

namespace cmd {

namespace {

namespace fs = std::filesystem;

std::string s(const QJsonObject& a, const char* key) { return a[key].toString().toStdString(); }
QString q(const std::string& v) { return QString::fromStdString(v); }

const QJsonObject kLib = param("string", "library name"), kCell = param("string", "cell name"),
                  kView = param("string", "view name: a schematic, or a config naming one");

// The cards a test bench may add after the netlist: analyses and their controls, one card each; `analysis` is set
// when it is one
std::string card(const QJsonValue& v, bool& analysis) {
    static const std::set<std::string> analyses = {".op", ".dc", ".ac", ".tran", ".noise", ".pz", ".sens", ".tf"};
    static const std::set<std::string> allowed = {".op", ".dc", ".ac", ".tran", ".noise", ".pz", ".sens", ".tf", ".options", ".option",
                                                  ".temp", ".param", ".ic", ".nodeset", ".save", ".include", ".lib", ".model", ".meas", ".measure"};
    const std::string c = v.toString().toStdString();
    std::string kw = c.substr(0, c.find_first_of(" \t"));
    for (auto& ch : kw) ch = (char)std::tolower((unsigned char)ch);
    if (c.find_first_of("\r\n") != std::string::npos || !allowed.count(kw))
        throw std::invalid_argument("card '" + c + "': one line starting with .op .dc .ac .tran .noise .pz .sens .tf .options .temp .param .ic "
                                    ".nodeset .save .include .lib .model or .meas");
    analysis = analysis || analyses.count(kw);
    return c;
}

fs::path simulationRoot(Context& c) { return fs::absolute(fs::path(c.workspace.file()).parent_path() / "simulation"); }


QJsonObject summary(const sim::Plot& p, int index) {
    QJsonArray names;
    for (const auto& n : p.vectors) names.append(q(n));
    QJsonObject o{{"index", index}, {"name", q(p.name)}, {"complex", p.complex}, {"vectors", names},
                  {"points", p.data.empty() ? 0 : (int)p.data[0].size()}};
    if (!p.complex && !p.data.empty() && p.data[0].size() == 1) {  // an operating point, a transfer function: the numbers
        QJsonObject values;
        for (size_t k = 0; k < p.vectors.size(); ++k) values[q(p.vectors[k])] = p.data[k][0].real();
        o["values"] = values;
    }
    return o;
}

}  // namespace

std::string runDirectory(Context& c, const std::string& run) {
    std::error_code e;
    const fs::path root = fs::weakly_canonical(simulationRoot(c), e), dir = fs::weakly_canonical(fs::path(run), e);
    const auto [r, d] = std::mismatch(root.begin(), root.end(), dir.begin(), dir.end());
    if (e || r != root.end() || !fs::is_regular_file(dir / "out.raw")) throw std::invalid_argument("no run '" + run + "' under " + root.string());
    return dir.string();
}

void addSimulationCommands(Registry& r) {
    r.add({"netlist", "Returns a schematic's (or a config view's) hierarchy as SPICE for TreeSPICE.",
           object({{"library", kLib}, {"cell", kCell}, {"view", kView}}),
           [](Context& c, const QJsonObject& a) { return QJsonValue(q(sim::netlist(c.workspace, s(a, "library"), s(a, "cell"), s(a, "view")))); },
           true});
    r.add({"simulate", "Netlists a schematic or config, adds the cards (analyses, .options, .include, ...) and runs it on TreeSPICE "
                       "in a worker process; returns the run directory and each analysis' vectors (an operating point's values).",
           object({{"library", kLib}, {"cell", kCell}, {"view", kView},
                   {"cards", QJsonObject{{"type", "array"}, {"description", "SPICE cards, one each: \".tran 1n 100n\", \".options reltol=1e-4\", ..."},
                                         {"items", QJsonObject{{"type", "string"}}}}}},
                  {{"timeout", param("integer", "seconds (default 600)")}}),
           [](Context& c, const QJsonObject& a) {
               std::string text = sim::netlist(c.workspace, s(a, "library"), s(a, "cell"), s(a, "view"));
               bool analysis = false;
               for (const auto& v : a["cards"].toArray()) text += card(v, analysis) + "\n";
               if (!analysis) throw std::invalid_argument("no analysis among the cards (.op, .dc, .ac, .tran, ...)");
               text += ".end\n";
               const int timeout = a.contains("timeout") ? a["timeout"].toInt() : 600;
               if (timeout <= 0) throw std::invalid_argument("timeout: a positive number of seconds");
               const fs::path base = simulationRoot(c) / s(a, "library") / s(a, "cell") / s(a, "view");
               fs::create_directories(base);
               fs::path dir;
               for (int n = 1;; ++n)  // the next free runN (atomic: another process may be starting one)
                   if (fs::create_directory(dir = base / ("run" + std::to_string(n)))) break;
               // ponytail: the caller waits for the run (right for scripts and MCP); ADE Explorer runs it in the background
               const sim::Run run = sim::simulate(c.simulator, dir.string(), text, std::chrono::seconds(timeout));
               QJsonArray plots;
               for (size_t k = 0; k < run.plots.size(); ++k) plots.append(summary(run.plots[k], (int)k));
               return QJsonValue(QJsonObject{{"run", q(dir.string())}, {"plots", plots}});
           },
           true});
    r.add({"results_get", "Returns a run's vectors for one analysis (by index): real values, or {re, im} for an AC analysis.",
           object({{"run", param("string", "the run directory simulate returned")}, {"plot", param("integer", "the analysis' index")}},
                  {{"vectors", QJsonObject{{"type", "array"}, {"description", "names (default: all)"}, {"items", QJsonObject{{"type", "string"}}}}}}),
           [](Context& c, const QJsonObject& a) {
               const auto plots = sim::readRaw((fs::path(runDirectory(c, s(a, "run"))) / "out.raw").string());
               const int k = a["plot"].toInt();
               if (k < 0 || k >= (int)plots.size()) throw std::invalid_argument("no plot " + std::to_string(k) + " (the run has " + std::to_string(plots.size()) + ")");
               const sim::Plot& p = plots[(size_t)k];
               std::set<std::string> want;
               for (const auto& v : a["vectors"].toArray()) want.insert(v.toString().toStdString());
               const bool all = want.empty();
               QJsonObject out;
               for (size_t v = 0; v < p.vectors.size(); ++v) {
                   if (!all && !want.erase(p.vectors[v])) continue;
                   QJsonArray re, im;
                   for (const auto& x : p.data[v]) re.append(x.real()), im.append(x.imag());
                   out[q(p.vectors[v])] = p.complex ? QJsonValue(QJsonObject{{"re", re}, {"im", im}}) : QJsonValue(re);
               }
               if (!want.empty()) throw std::invalid_argument("plot " + std::to_string(k) + " has no vector " + *want.begin());
               return QJsonValue(QJsonObject{{"name", q(p.name)}, {"complex", p.complex}, {"vectors", out}});
           },
           true});
    r.add({"wave_eval", "Evaluates a calculator expression (VT(out), riseTime(VT(out)), dB20(VF(out)), ...) on a run's results: "
                        "a number, {re, im}, or a waveform {x, y} ({x, re, im} when complex).",
           object({{"run", param("string", "the run directory (simulate's run, or an ADE point's dir)")},
                   {"expression", param("string", "a calculator expression (sim/calc.h)")}}),
           [](Context& c, const QJsonObject& a) {
               const auto plots = sim::readRaw((fs::path(runDirectory(c, s(a, "run"))) / "out.raw").string());
               const sim::Wave w = sim::evaluate(s(a, "expression"), sim::resultsLookup(plots));
               const bool complex = std::any_of(w.y.begin(), w.y.end(), [](const std::complex<double>& v) { return v.imag() != 0; });
               if (w.scalar()) return complex ? QJsonValue(QJsonObject{{"re", w.y[0].real()}, {"im", w.y[0].imag()}}) : QJsonValue(w.y[0].real());
               QJsonArray x, re, im;
               for (const double v : w.x) x.append(v);
               for (const auto& v : w.y) re.append(v.real()), im.append(v.imag());
               return QJsonValue(complex ? QJsonObject{{"x", x}, {"re", re}, {"im", im}} : QJsonObject{{"x", x}, {"y", re}});
           },
           true});
}

}  // namespace cmd
