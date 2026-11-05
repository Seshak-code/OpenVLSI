// Tempus's and Voltus's roles over an implementation run: static timing through OpenSTA, IR drop and electromigration through OpenROAD's PSM,
// with the routed design's own parasitics. Results land in the run's directory (signoff/), so they survive the app.
#include "cmd/registry.h"
#include "flow/rtl_flow.h"
#include "flow/subprocess.h"
#include "oadb/oadb.h"
#include "sim/ade.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace cmd {

namespace {

namespace fs = std::filesystem;
using namespace acutesim::compute::orchestration;

const QJsonObject kRun = param("string", "an implementation run: <design>/run.N");

std::string s(const QJsonObject& a, const char* key) { return a[key].toString().toStdString(); }
QString q(const std::string& text) { return QString::fromStdString(text); }

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

void writeJson(const fs::path& file, const QJsonObject& o) {
    const fs::path tmp = file.string() + ".tmp";
    std::ofstream(tmp) << QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString();
    fs::rename(tmp, file);
}

struct Run {
    fs::path dir, results, signoff;
    QJsonObject record;
    const flow::Platform* pdk;
};

Run runOf(Context& c, const QJsonObject& a) {
    static const QRegularExpression name("^[A-Za-z0-9_]+/run\\.[0-9]+$");
    if (!name.match(a["run"].toString()).hasMatch()) throw std::invalid_argument("an implementation run is named <design>/run.N");
    Run r;
    r.dir = fs::absolute(fs::path(c.workspace.file()).parent_path()) / "implementation" / s(a, "run");
    if (!fs::is_directory(r.dir)) throw std::invalid_argument("no implementation run " + s(a, "run"));
    r.record = readJson(r.dir / "run.json");
    r.pdk = flow::findPlatform(s(r.record, "platform"));
    if (!r.pdk) throw std::runtime_error("the run's platform is not known");
    r.results = r.dir / "results" / s(r.record, "platform") / s(r.record, "design") / "base";
    r.signoff = r.dir / "signoff";
    if (!fs::exists(r.results / "6_final.odb")) throw std::runtime_error("this run has no routed design (" + (r.results / "6_final.odb").string() + ")");
    return r;
}

// The script's common start: libraries, the routed design, its constraints, and the best parasitics the run has
std::string loadDesign(const Run& r, const RtlFlowTools& tools, bool* spef) {
    std::ostringstream t;
    for (const auto& lib : r.pdk->liberty) t << "read_liberty " << flow::platformFile(*r.pdk, tools.flowDir, lib) << "\n";
    t << "read_db " << (r.results / "6_final.odb").string() << "\nread_sdc " << (r.results / "6_final.sdc").string() << "\nsource " << flow::platformFile(*r.pdk, tools.flowDir, r.pdk->setRC) << "\n";
    *spef = fs::exists(r.results / "6_final.spef");
    if (*spef) t << "read_spef " << (r.results / "6_final.spef").string() << "\n";
    else t << "estimate_parasitics -global_routing\n";
    return t.str();
}

bool openroad(const RtlFlowTools& tools, const fs::path& dir, const std::string& script, const std::atomic<bool>* cancel, std::string* log, std::string* why) {
    fs::create_directories(dir);
    std::ofstream(dir / "run.tcl") << script << "exit\n";
    acutesim::orchestration::Subprocess p(tools.openroad, {"-no_init", "-exit", (dir / "run.tcl").string()});
    p.setWorkingDirectory(dir.string());
    if (cancel) p.setCancelFlag(cancel);
    const auto res = p.run();
    *log = res.stdout_output + res.stderr_output;
    std::ofstream(dir / "run.log") << *log;
    if (res.exit_code != 0) {
        const auto error = log->find("[ERROR");
        *why = error == std::string::npos ? "OpenROAD stopped with exit " + std::to_string(res.exit_code) : log->substr(error, log->find('\n', error) - error);
        return false;
    }
    return true;
}

// One path from OpenSTA's JSON, in nanoseconds and microns
QJsonObject shownPath(const QJsonObject& check) {
    QJsonArray steps;
    for (const auto& v : check["source_path"].toArray()) {
        const QJsonObject p = v.toObject();
        steps.append(QJsonObject{{"instance", p["instance"]}, {"cell", p["cell"]}, {"pin", p["pin"]}, {"net", p["net"]}, {"x_um", p["x"].toDouble() * 1e6}, {"y_um", p["y"].toDouble() * 1e6},
                                 {"arrival_ns", p["arrival"].toDouble() * 1e9}, {"slew_ns", p["slew"].toDouble() * 1e9}});
    }
    return {{"startpoint", check["startpoint"]}, {"endpoint", check["endpoint"]}, {"group", check["path_group"]}, {"clock", check["target_clock"]},
            {"slack_ns", check["slack"].toDouble() * 1e9}, {"arrival_ns", check["data_arrival_time"].toDouble() * 1e9}, {"required_ns", check["required_time"].toDouble() * 1e9}, {"steps", steps}};
}

QJsonArray pathsIn(const fs::path& file) {
    QJsonArray out;
    for (const auto& v : readJson(file)["checks"].toArray()) out.append(shownPath(v.toObject()));
    return out;
}

void analyzeTiming(const Run& r, int paths, const RtlFlowTools& tools, const std::atomic<bool>* cancel) {
    bool spef = false;
    std::string log, why;
    const std::string script = loadDesign(r, tools, &spef) + "report_wns\nreport_tns\nreport_clock_min_period\nreport_checks -path_delay max -group_path_count " + std::to_string(paths) +
                               " -format json > setup.json\nreport_checks -path_delay min -group_path_count " + std::to_string(paths) + " -format json > hold.json\n";
    const fs::path dir = r.signoff / "timing";
    QJsonObject out{{"state", "done"}, {"parasitics", spef ? "extracted (SPEF)" : "estimated from global routing"}};
    if (!openroad(tools, dir, script, cancel, &log, &why)) {
        out = {{"state", cancel && *cancel ? "stopped" : "failed"}, {"error", q(why)}};
        writeJson(r.signoff / "timing.json", out);
        return;
    }
    const double unit = r.pdk->nsPerTimeUnit;
    static const QRegularExpression wns("wns max (-?[0-9.eE+-]+)"), tns("tns max (-?[0-9.eE+-]+)"), period("(\\S+) period_min = (-?[0-9.eE+-]+) fmax = (-?[0-9.eE+-]+)");
    const QString text = q(log);
    const auto wm = wns.match(text), tm = tns.match(text);
    QJsonArray clocks;
    auto it = period.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        // period_min is in the library's time unit; fmax is MHz at a nanosecond library's scale: derive it from the period
        const double ns = m.captured(2).toDouble() * unit;
        clocks.append(QJsonObject{{"clock", m.captured(1)}, {"min_period_ns", ns}, {"fmax_mhz", ns > 0 ? 1000.0 / ns : 0.0}});
    }
    try {
        if (!wm.hasMatch() || !tm.hasMatch()) throw std::runtime_error("OpenSTA printed no WNS or TNS");
        if (clocks.isEmpty()) throw std::runtime_error("OpenSTA printed no clock period");
        out["wns_ns"] = wm.captured(1).toDouble() * unit;
        out["tns_ns"] = tm.captured(1).toDouble() * unit;
        out["clocks"] = clocks;
        out["setup"] = pathsIn(dir / "setup.json");
        out["hold"] = pathsIn(dir / "hold.json");
        out["met"] = out["setup"].toArray().size() > 0 && out["setup"].toArray()[0].toObject()["slack_ns"].toDouble() >= 0 && out["hold"].toArray().size() > 0 && out["hold"].toArray()[0].toObject()["slack_ns"].toDouble() >= 0;
    } catch (const std::exception& e) {
        out = {{"state", "failed"}, {"error", q(e.what())}};
    }
    writeJson(r.signoff / "timing.json", out);
}

void analyzePower(const Run& r, double supply, const RtlFlowTools& tools, const std::atomic<bool>* cancel) {
    bool spef = false;
    std::string log, why;
    std::ostringstream t;
    t << loadDesign(r, tools, &spef);
    for (const auto& [net, volts] : {std::pair{"VDD", supply}, {"VSS", 0.0}}) {
        t << "set_pdnsim_net_voltage -net " << net << " -voltage " << volts << "\n";
        t << "analyze_power_grid -net " << net << " -voltage_file ir_" << net << ".csv -enable_em -em_outfile em_" << net << ".csv\n";
    }
    const fs::path dir = r.signoff / "power";
    QJsonObject out{{"state", "done"}, {"supply_v", supply}};
    if (!openroad(tools, dir, t.str(), cancel, &log, &why)) {
        writeJson(r.signoff / "power.json", {{"state", cancel && *cancel ? "stopped" : "failed"}, {"error", q(why)}});
        return;
    }
    // Read from the voltage files themselves, not the printed report: every node is there
    QJsonArray nets;
    try {
        for (const auto& [net, rail] : {std::pair{"VDD", supply}, {"VSS", 0.0}}) {
            std::ifstream f(dir / (std::string("ir_") + net + ".csv"));
            if (!f) throw std::runtime_error(std::string("PSM wrote no voltages for ") + net);
            std::string line;
            std::getline(f, line);
            double worst = 0, sum = 0;
            long n = 0;
            std::string where;
            while (std::getline(f, line)) {
                const auto last = line.rfind(',');
                if (last == std::string::npos) continue;
                const double drop = std::fabs(std::atof(line.c_str() + last + 1) - rail);
                sum += drop;
                ++n;
                if (drop > worst) {
                    worst = drop;
                    where = line.substr(0, line.find(','));
                }
            }
            if (!n) throw std::runtime_error(std::string("no node in the voltages of ") + net);
            std::ifstream em(dir / (std::string("em_") + net + ".csv"));
            std::getline(em, line);
            double maxCurrent = 0;
            long resistors = 0;
            while (std::getline(em, line)) {
                const auto last = line.rfind(',');
                if (last == std::string::npos) continue;
                maxCurrent = std::max(maxCurrent, std::fabs(std::atof(line.c_str() + last + 1)));
                ++resistors;
            }
            nets.append(QJsonObject{{"net", net}, {"nodes", (double)n}, {"worst_drop_mv", worst * 1e3}, {"average_drop_mv", sum / (double)n * 1e3}, {"worst_percent", supply > 0 ? worst / supply * 100 : 0.0},
                                    {"worst_instance", q(where)}, {"max_current_ma", maxCurrent * 1e3}, {"resistors", (double)resistors}});
        }
        out["nets"] = nets;
    } catch (const std::exception& e) {
        out = {{"state", "failed"}, {"error", q(e.what())}};
    }
    writeJson(r.signoff / "power.json", out);
}

}  // namespace

void addSignoffCommands(Registry& r) {
    r.add({"timing_analyze", "Static timing of a run's routed design through OpenSTA, with its extracted parasitics: WNS, TNS, each clock's minimum period, and the worst setup and hold paths step by step. "
                             "Waits and returns it, or with wait false starts it in the background (poll timing_report).",
           object({{"run", kRun}}, {{"paths", param("integer", "how many paths per group (default 20)")}, {"wait", param("boolean", "wait for it (default true)")}}),
           [](Context& c, const QJsonObject& a) {
               const Run run = runOf(c, a);
               const RtlFlowTools tools = RtlFlowTools::detect();
               if (tools.openroad.empty() || tools.flowDir.empty()) throw std::runtime_error("OpenROAD and the platform are needed: " + tools.missing());
               const int paths = a["paths"].toInt(20);
               if (paths < 1 || paths > 10000) throw std::invalid_argument("paths is 1 to 10000");
               fs::create_directories(run.signoff);
               writeJson(run.signoff / "timing.json", {{"state", "running"}});
               if (!a.contains("wait") || a["wait"].toBool()) {
                   analyzeTiming(run, paths, tools, nullptr);
                   return QJsonValue(readJson(run.signoff / "timing.json"));
               }
               jobs(c).start((run.signoff / "timing").string(), [run, paths, tools](const std::atomic<bool>& cancel) { analyzeTiming(run, paths, tools, &cancel); });
               return QJsonValue(QJsonObject{{"state", "running"}});
           },
           true});
    r.add({"timing_report", "A run's last timing analysis: its state and results (see timing_analyze); an error when it has none.", object({{"run", kRun}}),
           [](Context& c, const QJsonObject& a) {
               const Run run = runOf(c, a);
               if (!fs::exists(run.signoff / "timing.json")) throw std::runtime_error("this run has no timing analysis yet (timing_analyze)");
               return QJsonValue(readJson(run.signoff / "timing.json"));
           },
           true});
    r.add({"power_analyze", "IR drop and electromigration of a run's power grid through OpenROAD's PSM, net by net: worst and average drop, where it is worst, the largest current in a grid resistor. "
                            "supply_v defaults to the platform's. Waits and returns it, or with wait false starts it in the background (poll power_report).",
           object({{"run", kRun}}, {{"supply_v", param("number", "the supply, in volts")}, {"wait", param("boolean", "wait for it (default true)")}}),
           [](Context& c, const QJsonObject& a) {
               const Run run = runOf(c, a);
               const RtlFlowTools tools = RtlFlowTools::detect();
               if (tools.openroad.empty() || tools.flowDir.empty()) throw std::runtime_error("OpenROAD and the platform are needed: " + tools.missing());
               const double supply = a.contains("supply_v") ? a["supply_v"].toDouble() : run.pdk->supplyVolts;
               if (!(supply > 0)) throw std::invalid_argument("the supply must be above 0 V");
               fs::create_directories(run.signoff);
               writeJson(run.signoff / "power.json", {{"state", "running"}});
               if (!a.contains("wait") || a["wait"].toBool()) {
                   analyzePower(run, supply, tools, nullptr);
                   return QJsonValue(readJson(run.signoff / "power.json"));
               }
               jobs(c).start((run.signoff / "power").string(), [run, supply, tools](const std::atomic<bool>& cancel) { analyzePower(run, supply, tools, &cancel); });
               return QJsonValue(QJsonObject{{"state", "running"}});
           },
           true});
    r.add({"power_report", "A run's last power-grid analysis: its state and results (see power_analyze); an error when it has none.", object({{"run", kRun}}),
           [](Context& c, const QJsonObject& a) {
               const Run run = runOf(c, a);
               if (!fs::exists(run.signoff / "power.json")) throw std::runtime_error("this run has no power-grid analysis yet (power_analyze)");
               return QJsonValue(readJson(run.signoff / "power.json"));
           },
           true});
    r.add({"power_map", "The voltage drop at every node of a power net, for a map: [x, y, drop] in microns and millivolts, with the worst.", object({{"run", kRun}, {"net", param("string", "VDD or VSS")}}),
           [](Context& c, const QJsonObject& a) {
               const Run run = runOf(c, a);
               const QString net = a["net"].toString();
               if (net != "VDD" && net != "VSS") throw std::invalid_argument("net is VDD or VSS");
               const QJsonObject report = fs::exists(run.signoff / "power.json") ? readJson(run.signoff / "power.json") : QJsonObject{};
               if (!report.contains("supply_v")) throw std::runtime_error("this run has no finished power-grid analysis (power_analyze)");
               const double rail = net == "VDD" ? report["supply_v"].toDouble() : 0.0;
               std::ifstream f(run.signoff / "power" / ("ir_" + net.toStdString() + ".csv"));
               if (!f) throw std::runtime_error("no voltages for " + net.toStdString());
               std::string line;
               std::getline(f, line);
               QJsonArray nodes;
               double worst = 0;
               while (std::getline(f, line)) {
                   // Instance, terminal, layer, x, y, voltage
                   std::istringstream cells(line);
                   std::string cell;
                   std::vector<std::string> v;
                   while (std::getline(cells, cell, ',')) v.push_back(cell);
                   if (v.size() != 6) continue;
                   const double drop = std::fabs(std::atof(v[5].c_str()) - rail) * 1e3;
                   worst = std::max(worst, drop);
                   nodes.append(QJsonArray{std::atof(v[3].c_str()), std::atof(v[4].c_str()), drop});
               }
               return QJsonValue(QJsonObject{{"net", net}, {"worst_mv", worst}, {"nodes", nodes}});
           },
           true});
    r.add({"flow_artifacts", "The files an implementation run made, stage by stage: netlists, databases, constraints, parasitics, GDS, reports. For tools outside this app.", object({{"run", kRun}}),
           [](Context& c, const QJsonObject& a) {
               const Run run = runOf(c, a);
               QJsonObject out;
               const auto list = [](const fs::path& d) {
                   QJsonArray files;
                   if (fs::is_directory(d)) {
                       std::vector<std::string> names;
                       for (const auto& e : fs::directory_iterator(d))
                           if (e.is_regular_file()) names.push_back(e.path().string());
                       std::sort(names.begin(), names.end());
                       for (const auto& n : names) files.append(q(n));
                   }
                   return files;
               };
               const fs::path tail = fs::path(s(run.record, "platform")) / s(run.record, "design") / "base";
               out["results"] = list(run.dir / "results" / tail);
               out["reports"] = list(run.dir / "reports" / tail);
               out["logs"] = list(run.dir / "logs" / tail);
               out["signoff"] = list(run.signoff / "timing");
               QJsonArray power = list(run.signoff / "power");
               for (const auto& v : power) out["signoff"] = [&] { QJsonArray j = out["signoff"].toArray(); j.append(v); return j; }();
               return QJsonValue(out);
           },
           true});
}

}  // namespace cmd
