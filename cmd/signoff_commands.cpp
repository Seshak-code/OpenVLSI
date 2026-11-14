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
#include <map>
#include <sstream>
#include <stdexcept>
#include <thread>

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
// (sdc: other constraints than the run's own; parasitics: another SPEF than the run's own)
std::string loadDesign(const Run& r, const RtlFlowTools& tools, bool* spef, const std::string& corner = {}, const fs::path& sdc = {}, const fs::path& parasitics = {}) {
    std::ostringstream t;
    for (const auto& lib : flow::libertyFor(*r.pdk, corner)) t << "read_liberty " << flow::platformFile(*r.pdk, tools.flowDir, lib) << "\n";
    t << "read_db " << (r.results / "6_final.odb").string() << "\nread_sdc {" << (sdc.empty() ? r.results / "6_final.sdc" : sdc).string() << "}\nsource "
      << flow::platformFile(*r.pdk, tools.flowDir, r.pdk->setRC) << "\n";
    *spef = fs::exists(r.results / "6_final.spef");
    if (*spef) t << "read_spef " << (parasitics.empty() ? r.results / "6_final.spef" : parasitics).string() << "\n";
    else t << "estimate_parasitics -global_routing\n";
    return t.str();
}

// One analysis view: a mode's constraints at a corner's libraries, with the extracted parasitics derated
struct View {
    std::string mode, corner;
    fs::path sdc;       // empty: the run's own
    double r = 1, c = 1;  // what the extracted resistance and capacitance are multiplied by
};

// The run's SPEF with every resistance and capacitance scaled, by its unit lines: the values themselves stay as extracted.
// ponytail: one factor for all layers, and a copy of the file per derated view; a rule file per RC corner in the platform's
// extraction is the upgrade, when a PDK ships one.
void derateSpef(const fs::path& from, const fs::path& to, double r, double c) {
    std::ifstream in(from);
    std::ofstream out(to);
    out.precision(12);
    std::string line;
    int scaled = 0;
    while (std::getline(in, line)) {
        const bool res = line.rfind("*R_UNIT ", 0) == 0, cap = line.rfind("*C_UNIT ", 0) == 0;
        if (res || cap) {
            std::istringstream parts(line.substr(8));
            double value = 0;
            std::string unit;
            if (!(parts >> value >> unit) || !(value > 0)) throw std::runtime_error("the SPEF's unit line does not read: " + line);
            out << line.substr(0, 8) << value * (res ? r : c) << " " << unit << "\n";
            ++scaled;
        } else {
            out << line << "\n";
        }
    }
    if (scaled != 2) throw std::runtime_error("the SPEF has no *R_UNIT and *C_UNIT to scale: " + from.string());
    if (!out.flush()) throw std::runtime_error("cannot write " + to.string());
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

// One view, in its own OpenROAD: its libraries, the routed design, its constraints and its parasitics. Throws why it could not.
// ponytail: a process per view; define_corners and one process per mode is the upgrade when runs get long.
QJsonObject timingAt(const Run& r, const View& view, int paths, const RtlFlowTools& tools, const std::atomic<bool>* cancel) {
    bool spef = false;
    std::string log, why;
    const std::string& corner = view.corner;
    const std::string label = view.mode + " " + corner;
    const fs::path dir = r.signoff / "timing" / view.mode / corner;
    fs::create_directories(dir);
    const bool derated = view.r != 1 || view.c != 1;
    if (derated) {
        if (!fs::exists(r.results / "6_final.spef")) throw std::runtime_error(label + ": the run has no extracted parasitics to derate");
        derateSpef(r.results / "6_final.spef", dir / "derated.spef", view.r, view.c);
    }
    const std::string script = loadDesign(r, tools, &spef, corner, view.sdc, derated ? dir / "derated.spef" : fs::path()) +
                               "report_wns\nreport_tns\nreport_clock_min_period\nreport_checks -path_delay max -group_path_count " + std::to_string(paths) +
                               " -format json > setup.json\nreport_checks -path_delay min -group_path_count " + std::to_string(paths) + " -format json > hold.json\n";
    if (!openroad(tools, dir, script, cancel, &log, &why)) throw std::runtime_error(label + ": " + why);
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
    if (!wm.hasMatch() || !tm.hasMatch()) throw std::runtime_error(label + ": OpenSTA printed no WNS or TNS");
    if (clocks.isEmpty()) throw std::runtime_error(label + ": OpenSTA printed no clock period (do its constraints create a clock?)");
    const auto tagged = [&](const fs::path& file) {
        QJsonArray out;
        for (const auto& v : pathsIn(file)) {
            QJsonObject p = v.toObject();
            p["corner"] = q(corner);
            p["mode"] = q(view.mode);
            out.append(p);
        }
        return out;
    };
    const QJsonArray setup = tagged(dir / "setup.json"), hold = tagged(dir / "hold.json");
    if (setup.isEmpty() || hold.isEmpty()) throw std::runtime_error(label + ": OpenSTA reported no paths");
    QJsonArray libs;
    for (const auto& lib : flow::libertyFor(*r.pdk, corner)) libs.append(q(fs::path(lib).filename().string()));
    return {{"mode", q(view.mode)}, {"corner", q(corner)}, {"liberty", libs}, {"rc", QJsonObject{{"r", view.r}, {"c", view.c}}},
            {"wns_ns", wm.captured(1).toDouble() * unit}, {"tns_ns", tm.captured(1).toDouble() * unit}, {"clocks", clocks},
            {"setup_slack_ns", setup[0].toObject()["slack_ns"]}, {"hold_slack_ns", hold[0].toObject()["slack_ns"]},
            {"met", setup[0].toObject()["slack_ns"].toDouble() >= 0 && hold[0].toObject()["slack_ns"].toDouble() >= 0}, {"setup", setup}, {"hold", hold},
            {"parasitics", spef ? "extracted (SPEF)" : "estimated from global routing"}};
}

// Every view asked for, at once; the report's top level is the worst of them, and each path says whose it is. `analysis` is
// what was asked, kept with the result so that it can be run again.
void analyzeTiming(const Run& r, const std::vector<View>& views, int paths, const QJsonObject& analysis, const RtlFlowTools& tools, const std::atomic<bool>* cancel) {
    std::vector<QJsonObject> results(views.size());
    std::vector<std::string> errors(views.size());
    std::vector<std::thread> threads;
    for (size_t k = 0; k < views.size(); ++k)
        threads.emplace_back([&, k] {
            try {
                results[k] = timingAt(r, views[k], paths, tools, cancel);
            } catch (const std::exception& e) {
                errors[k] = e.what();
            }
        });
    for (auto& t : threads) t.join();
    QJsonObject out;
    for (const auto& e : errors)
        if (!e.empty()) {  // a view that did not report is no pass: the whole analysis fails with its reason
            writeJson(r.signoff / "timing.json", {{"state", cancel && *cancel ? "stopped" : "failed"}, {"error", q(e)}, {"analysis", analysis}});
            return;
        }
    QJsonArray perCorner, setup, hold;
    double wns = 0, tns = 0;
    bool met = true;
    std::map<QString, QJsonObject> clocks;
    std::vector<QJsonObject> allSetup, allHold;
    for (const QJsonObject& c : results) {
        QJsonObject brief = c;
        brief.remove("setup");
        brief.remove("hold");
        perCorner.append(brief);
        wns = std::min(wns, c["wns_ns"].toDouble());
        tns = std::min(tns, c["tns_ns"].toDouble());
        met = met && c["met"].toBool();
        for (const auto& v : c["clocks"].toArray()) {
            QJsonObject clock = v.toObject();
            clock["corner"] = c["corner"];
            clock["mode"] = c["mode"];
            auto [it, fresh] = clocks.try_emplace(clock["clock"].toString(), clock);
            if (!fresh && clock["min_period_ns"].toDouble() > it->second["min_period_ns"].toDouble()) it->second = clock;
        }
        for (const auto& v : c["setup"].toArray()) allSetup.push_back(v.toObject());
        for (const auto& v : c["hold"].toArray()) allHold.push_back(v.toObject());
    }
    const auto worstFirst = [&](std::vector<QJsonObject>& all) {
        std::stable_sort(all.begin(), all.end(), [](const QJsonObject& a, const QJsonObject& b) { return a["slack_ns"].toDouble() < b["slack_ns"].toDouble(); });
        QJsonArray list;
        for (size_t k = 0; k < all.size() && (int)k < paths; ++k) list.append(all[k]);
        return list;
    };
    QJsonArray clockList;
    for (const auto& [name, clock] : clocks) clockList.append(clock);
    writeJson(r.signoff / "timing.json", {{"state", "done"}, {"parasitics", results[0]["parasitics"]}, {"corners", perCorner}, {"wns_ns", wns}, {"tns_ns", tns}, {"clocks", clockList},
                                          {"setup", worstFirst(allSetup)}, {"hold", worstFirst(allHold)}, {"met", met}, {"analysis", analysis}});
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
    r.add({"timing_analyze", "Static timing of a run's routed design through OpenSTA, in every mode at every corner of its platform (or those named), with its extracted parasitics: "
                             "per mode and corner (the entries of corners) WNS, TNS, each clock's minimum period and whether it is met; overall the worst of them, and the worst setup "
                             "and hold paths across them step by step, each saying its mode and corner. A mode is a set of constraints (an SDC file); with none given there is one, "
                             "func, the run's own. rc derates the extracted resistance and capacitance at a corner. analysis in the result is what was asked, to run it again. "
                             "Waits and returns it, or with wait false starts it in the background (poll timing_report).",
           object({{"run", kRun}}, {{"paths", param("integer", "how many paths per group (default 20)")},
                                    {"corners", QJsonObject{{"type", "array"}, {"description", "corner names, as slow, typ, fast (default all the platform has)"}, {"items", QJsonObject{{"type", "string"}}}}},
                                    {"rc", QJsonObject{{"type", "object"}, {"description", "per corner, what to multiply the extracted resistance and capacitance by, as {\"slow\": {\"r\": 1.15, \"c\": 1.1}}; "
                                                                                           "a corner not named keeps what was extracted"}}},
                                    {"modes", QJsonObject{{"type", "array"}, {"description", "the modes, as [{\"name\": \"func\"}, {\"name\": \"scan\", \"sdc\": \"/abs/scan.sdc\"}]; sdc left out is the run's own constraints"},
                                                          {"items", object({{"name", param("string", "the mode's name: letters, digits, _")}}, {{"sdc", param("string", "its constraints: an absolute path")}})}}},
                                    {"wait", param("boolean", "wait for it (default true)")}}),
           [](Context& c, const QJsonObject& a) {
               const Run run = runOf(c, a);
               const RtlFlowTools tools = RtlFlowTools::detect();
               if (tools.openroad.empty() || tools.flowDir.empty()) throw std::runtime_error("OpenROAD and the platform are needed: " + tools.missing());
               const int paths = a["paths"].toInt(20);
               if (paths < 1 || paths > 10000) throw std::invalid_argument("paths is 1 to 10000");
               std::vector<std::string> corners;
               for (const auto& v : a["corners"].toArray()) {
                   flow::libertyFor(*run.pdk, v.toString().toStdString());
                   if (v.toString().isEmpty() || std::find(corners.begin(), corners.end(), v.toString().toStdString()) != corners.end()) throw std::invalid_argument("corners are named once each");
                   corners.push_back(v.toString().toStdString());
               }
               if (corners.empty())
                   for (const auto& [name, files] : run.pdk->corners) corners.push_back(name);
               if (corners.empty()) throw std::runtime_error("platform " + run.pdk->name + " has no timing corners");
               const QJsonObject rc = a["rc"].toObject();
               for (auto it = rc.begin(); it != rc.end(); ++it) {
                   if (std::find(corners.begin(), corners.end(), it.key().toStdString()) == corners.end()) throw std::invalid_argument("rc names " + it.key().toStdString() + ", which is not a corner analysed");
                   const QJsonObject f = it.value().toObject();
                   for (const QString& k : f.keys())
                       if (k != "r" && k != "c") throw std::invalid_argument("rc." + it.key().toStdString() + " takes r and c");
                   for (const char* k : {"r", "c"})
                       if (f.contains(k) && !(f[k].isDouble() && f[k].toDouble() > 0 && f[k].toDouble() <= 10)) throw std::invalid_argument("rc." + it.key().toStdString() + "." + k + " is a factor above 0, to 10");
                   if (f.isEmpty()) throw std::invalid_argument("rc." + it.key().toStdString() + " takes r and c");
               }
               static const QRegularExpression modeName("^[A-Za-z0-9_]+$");
               std::vector<std::pair<std::string, fs::path>> modes;
               for (const auto& v : a["modes"].toArray()) {
                   const QJsonObject m = v.toObject();
                   const std::string name = s(m, "name");
                   if (!modeName.match(m["name"].toString()).hasMatch()) throw std::invalid_argument("a mode's name is letters, digits and _: '" + name + "'");
                   for (const auto& [other, file] : modes)
                       if (other == name) throw std::invalid_argument("modes are named once each: " + name);
                   fs::path sdc;
                   if (m.contains("sdc")) {
                       sdc = s(m, "sdc");
                       if (!sdc.is_absolute() || !fs::is_regular_file(sdc)) throw std::invalid_argument("mode " + name + ": no constraints file at the absolute path '" + sdc.string() + "'");
                       if (sdc.string().find_first_of("{}") != std::string::npos) throw std::invalid_argument("mode " + name + ": a path with braces cannot be passed to the tool");
                   }
                   modes.emplace_back(name, sdc);
               }
               if (a.contains("modes") && modes.empty()) throw std::invalid_argument("modes names at least one");
               if (modes.empty()) modes.emplace_back("func", fs::path());
               std::vector<View> views;
               for (const auto& [mode, sdc] : modes)
                   for (const auto& corner : corners) {
                       const QJsonObject f = rc[q(corner)].toObject();
                       views.push_back({mode, corner, sdc, f["r"].toDouble(1), f["c"].toDouble(1)});
                   }
               if (!rc.isEmpty() && !fs::exists(run.results / "6_final.spef")) throw std::runtime_error("the run has no extracted parasitics to derate");
               QJsonObject analysis{{"paths", paths}};
               for (const char* k : {"corners", "rc", "modes"})
                   if (a.contains(k)) analysis[k] = a[k];
               fs::create_directories(run.signoff);
               writeJson(run.signoff / "timing.json", {{"state", "running"}, {"analysis", analysis}});
               if (!a.contains("wait") || a["wait"].toBool()) {
                   analyzeTiming(run, views, paths, analysis, tools, nullptr);
                   return QJsonValue(readJson(run.signoff / "timing.json"));
               }
               jobs(c).start((run.signoff / "timing").string(), [run, views, paths, analysis, tools](const std::atomic<bool>& cancel) { analyzeTiming(run, views, paths, analysis, tools, &cancel); });
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
    r.add({"flow_stage_layout", "Brings a run's layout at a stage (floorplan, place, cts or route) into a library of its own, standard cells' own layouts merged in, with the platform's layer colours; returns the library, cell and view.",
           object({{"run", kRun}, {"stage", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"floorplan", "place", "cts", "route"}}, {"description", "the stage"}}}}),
           [](Context& c, const QJsonObject& a) {
               const Run run = runOf(c, a);
               static const std::map<QString, std::string> odbs = {{"floorplan", "2_floorplan.odb"}, {"place", "3_place.odb"}, {"cts", "4_cts.odb"}, {"route", "5_route.odb"}};
               const auto odb = odbs.find(a["stage"].toString());
               if (odb == odbs.end()) throw std::invalid_argument("a stage is floorplan, place, cts or route");
               if (!fs::exists(run.results / odb->second)) throw std::runtime_error("this run has no " + odb->second);
               const std::string design = s(run.record, "design"), top = s(run.record, "top"), stage = s(a, "stage");
               const std::string library = design + "_run" + run.dir.filename().string().substr(4) + "_" + stage;
               const QJsonObject out{{"library", q(library)}, {"cell", q(top)}, {"view", "layout"}};
               const fs::path file = run.dir / ("layout_" + stage + ".oalib");
               const auto known = c.workspace.libraries();
               if (std::find(known.begin(), known.end(), library) != known.end()) return QJsonValue(out);
               if (fs::exists(file)) {
                   c.workspace.defineLibrary(library, file.string());
                   return QJsonValue(out);
               }
               const RtlFlowTools tools = RtlFlowTools::detect();
               if (tools.openroad.empty() || tools.klayout.empty() || tools.flowDir.empty()) throw std::runtime_error("OpenROAD, KLayout and the platform are needed: " + tools.missing());
               const fs::path dir = run.signoff / "stages" / stage;
               std::string log, why;
               if (!openroad(tools, dir, "read_db " + (run.results / odb->second).string() + "\nwrite_def stage.def\n", nullptr, &log, &why)) throw std::runtime_error("OpenROAD: " + why);
               const fs::path tech = run.dir / "objects" / s(run.record, "platform") / design / "base" / "klayout.lyt";
               if (!fs::exists(tech)) throw std::runtime_error("this run has no KLayout technology (" + tech.string() + ")");
               std::string cells;
               for (const auto& g : run.pdk->cellGds) cells += (cells.empty() ? "" : " ") + flow::platformFile(*run.pdk, tools.flowDir, g);
               acutesim::orchestration::Subprocess k(tools.klayout, {"-zz", "-rd", "design_name=" + top, "-rd", "in_def=" + (dir / "stage.def").string(), "-rd", "in_files=" + cells, "-rd", "seal_file=", "-rd",
                                                                    "out_file=" + (dir / "stage.gds").string(), "-rd", "tech_file=" + tech.string(), "-rd", "layer_map=", "-r", (fs::path(tools.flowDir) / "util" / "def2stream.py").string()});
               k.setWorkingDirectory(dir.string());
               if (!tools.binDir.empty()) {
                   const char* path = std::getenv("PATH");
                   k.setEnvironment({{"PATH", tools.binDir + ":" + (path ? path : "")}});
               }
               const auto res = k.run();
               std::ofstream(dir / "stream.log") << res.stdout_output << res.stderr_output;
               if (res.exit_code != 0 || !fs::exists(dir / "stage.gds")) throw std::runtime_error("KLayout could not stream this stage: see " + (dir / "stream.log").string());
               oadb::Library& l = c.workspace.createLibrary(library, file.string());
               const std::string lyp = run.pdk->layerProperties.empty() ? std::string() : flow::platformFile(*run.pdk, tools.flowDir, run.pdk->layerProperties);
               importGds(l, (dir / "stage.gds").string(), lyp);
               if (!run.pdk->tech.empty()) l.setLibraryProp("tech", "\"" + run.pdk->tech + "\"");
               return QJsonValue(out);
           }});
    r.add({"flow_artifacts", "The files an implementation run made, stage by stage: netlists, databases, constraints, parasitics, GDS, reports. For tools outside this app.", object({{"run", kRun}}),
           [](Context& c, const QJsonObject& a) {
               const Run run = runOf(c, a);
               QJsonObject out;
               const auto list = [](const fs::path& d) {
                   QJsonArray files;
                   if (fs::is_directory(d)) {
                       std::vector<std::string> names;
                       for (const auto& e : fs::recursive_directory_iterator(d))  // sign-off keeps a folder per corner
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
