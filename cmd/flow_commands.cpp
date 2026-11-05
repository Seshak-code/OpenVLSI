// The digital flow's commands: its tools and platforms, and its runs: RTL to GDS through ORFS (flow/rtl_flow.h), waited
// for or in the background. A run is a directory, implementation/<design>/run.N beside the workspace: run.json is its
// record (written whole, by the run alone), flow.log what its tools printed, the rest ORFS's own work tree. Everything
// read back comes from those files, so runs survive the app. flow_report reads ORFS's per-stage metrics files and the
// decks' reports; what is not there is an error in that stage's entry.
#include "cmd/registry.h"
#include "flow/rtl_flow.h"
#include "flow/subprocess.h"
#include "oadb/oadb.h"
#include "sim/ade.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <tuple>
#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#endif

namespace cmd {

namespace {

namespace fs = std::filesystem;
using namespace acutesim::compute::orchestration;

constexpr const char* kStages[] = {"synth", "floorplan", "place", "cts", "route", "finish", "drc", "lvs"};
constexpr int kDrc = 6, kLvs = 7;

const QJsonObject kRun = param("string", "a run: <design>/run.N");

double now() { return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count(); }
std::string s(const QJsonObject& a, const char* key) { return a[key].toString().toStdString(); }
QString q(const std::string& text) { return QString::fromStdString(text); }

fs::path root(Context& c) { return fs::absolute(fs::path(c.workspace.file()).parent_path() / "implementation"); }
fs::path runPath(Context& c, const QJsonObject& a) {
    static const QRegularExpression name("^[A-Za-z0-9_]+/run\\.[0-9]+$");
    if (!name.match(a["run"].toString()).hasMatch()) throw std::invalid_argument("a run is named <design>/run.N");
    const fs::path d = root(c) / s(a, "run");
    if (!fs::is_directory(d)) throw std::invalid_argument("no run " + s(a, "run"));
    return d;
}
sim::Jobs& jobs(Context& c) {
    if (!c.jobs) c.jobs = std::make_shared<sim::Jobs>();
    return *c.jobs;
}

bool processAlive(qint64 pid) {
#ifdef _WIN32
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!h) return false;
    const bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return alive;
#else
    return kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
#endif
}

bool writeRecord(const fs::path& dir, const QJsonObject& rec) {
    const fs::path tmp = dir / "run.json.tmp";
    {
        std::ofstream f(tmp);
        f << QJsonDocument(rec).toJson().toStdString();
        if (!f.flush()) return false;
    }
    std::error_code ec;
    fs::rename(tmp, dir / "run.json", ec);  // readers in any process see the old record or the new one
    return !ec;
}

// A run's record. One still "running" whose process is gone was interrupted: its tools may have died with it or not.
// ponytail: a pid reused by another process keeps such a run "running"; a heartbeat would close that
QJsonObject readRecord(const fs::path& dir) {
    std::ifstream f(dir / "run.json");
    if (!f) throw std::runtime_error("no record in " + dir.string());
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(text), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject() || !doc.object()["state"].isString() || !doc.object()["stages"].isArray())
        throw std::runtime_error("malformed record in " + dir.string());
    QJsonObject rec = doc.object();
    if (rec["state"] == "running" && !processAlive(rec["pid"].toInteger())) {
        rec["state"] = "interrupted";
        rec["error"] = "the process running it (" + QString::number(rec["pid"].toInteger()) + ") is gone";
        QJsonArray stages = rec["stages"].toArray();
        for (int i = 0; i < stages.size(); ++i)
            if (auto st = stages[i].toObject(); st["state"] == "running") {
                st["state"] = "interrupted";
                stages[i] = st;
            }
        rec["stages"] = stages;
    }
    return rec;
}

// The record as the commands show it: elapsed seconds in place of start and end times
QJsonObject shown(const QJsonObject& rec) {
    const double t = now();
    QJsonObject out = rec;
    out.remove("pid");
    auto elapsed = [t](const QJsonObject& o) { return (o.contains("finished") ? o["finished"].toDouble() : t) - o["started"].toDouble(); };
    QJsonArray stages;
    for (const auto& v : rec["stages"].toArray()) {
        QJsonObject st = v.toObject();
        if (st.contains("started") && st["state"] != "interrupted") st["elapsed_s"] = elapsed(st);
        st.remove("started");
        st.remove("finished");
        stages.append(st);
    }
    out["stages"] = stages;
    if (rec["state"] != "interrupted") out["elapsed_s"] = elapsed(rec);
    return out;
}

// The last n lines of what the run's tools printed
QJsonArray logTail(const fs::path& file, int n) {
    QJsonArray out;
    std::ifstream f(file, std::ios::binary | std::ios::ate);
    if (!f || n == 0) return out;
    constexpr std::streamoff kWindow = 1 << 16;  // ponytail: lines beyond the file's last 64 KiB are not reachable here
    const std::streamoff size = f.tellg(), from = std::max<std::streamoff>(0, size - kWindow);
    f.seekg(from);
    std::string text(static_cast<size_t>(size - from), '\0');
    f.read(text.data(), size - from);
    QStringList lines = QString::fromUtf8(text).split('\n');
    if (from > 0) lines.removeFirst();  // cut mid-line
    if (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
    for (qsizetype i = std::max<qsizetype>(0, lines.size() - n); i < lines.size(); ++i) out.append(lines[i]);
    return out;
}

// Runs the flow and keeps the record: which stage (ORFS's numbered steps, grouped), each one's times, how it ended. On
// any thread: it touches the run's directory only, never the workspace.
void execute(const fs::path& dir, QJsonObject rec, const RtlFlowConfig& cfg, const RtlFlowTools& tools, const std::atomic<bool>* cancel) {
    std::ofstream log(dir / "flow.log", std::ios::app);
    QJsonArray stages = rec["stages"].toArray();
    int current = -1;
    auto patch = [&](int i, const QString& state, double t, const char* time) {
        QJsonObject st = stages[i].toObject();
        st["state"] = state;
        st[time] = t;
        stages[i] = st;
    };
    auto save = [&] {
        rec["stages"] = stages;
        rec["stage"] = current < 0 ? QJsonValue(QJsonValue::Null) : QJsonValue(kStages[current]);
        writeRecord(dir, rec);  // not written: the record on disk lags until the next one is (the log still says what happens)
    };
    auto enter = [&](int next) {
        if (next <= current) return;
        const double t = now();
        if (current >= 0) patch(current, "done", t, "finished");
        for (int i = current + 1; i < next; ++i)
            if (stages[i].toObject()["state"] == "pending") patch(i, "skipped", t, "finished");
        current = next;
        patch(current, "running", t, "started");
        save();
    };
    QString error;
    try {
        const RtlFlowResult r = runRtlToGds(cfg, tools,
            [&](const std::string& line, bool) { log << line << std::endl; },
            [&](const std::string& stage, double) {
                if (stage == "synthesis") enter(0);
                else if (stage == "DRC") enter(kDrc);
                else if (stage == "LVS") enter(kLvs);
                else if (stage[0] >= '1' && stage[0] <= '6') enter(stage[0] - '1');
            },
            cancel);
        error = q(r.error);
    } catch (const std::exception& e) {
        error = e.what();
    }
    const bool stopped = !error.isEmpty() && cancel && cancel->load();
    const double t = now();
    if (current >= 0) patch(current, error.isEmpty() ? "done" : stopped ? "stopped" : "failed", t, "finished");
    if (error.isEmpty())
        for (int i = current + 1; i < stages.size(); ++i)
            if (stages[i].toObject()["state"] == "pending") patch(i, "skipped", t, "finished");
    rec["state"] = error.isEmpty() ? "done" : stopped ? "stopped" : "failed";
    rec["finished"] = t;
    if (!error.isEmpty()) rec["error"] = error;
    current = -1;
    save();
}

// One stage's metrics file from ORFS: each wanted key (<prefix>__<key>) scaled into out; what is missing goes in errors
struct Metric { const char* name; const char* key; double scale; };
void readMetrics(const fs::path& file, const QString& prefix, std::initializer_list<Metric> wanted, const flow::Platform& pdk,
                 QJsonObject& out, QStringList& errors) {
    std::ifstream f(file);
    if (!f) {
        errors.append("no report: " + q(file.string()));
        return;
    }
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    QJsonParseError pe;
    const QJsonObject m = QJsonDocument::fromJson(QByteArray::fromStdString(text), &pe).object();
    if (pe.error != QJsonParseError::NoError) {
        errors.append("malformed report: " + q(file.string()) + ": " + pe.errorString());
        return;
    }
    const QJsonValue unit = m["run__flow__platform__time_units"];
    if (unit.isString() && unit != q(pdk.timeUnit))
        errors.append(q(file.string()) + " is in " + unit.toString() + ", the platform says " + q(pdk.timeUnit));
    for (const Metric& w : wanted) {
        const QString key = prefix + "__" + w.key;
        if (m[key].isDouble()) out[w.name] = m[key].toDouble() * w.scale;
        else errors.append(q(file.string()) + " has no number for " + key);
    }
}

// flow_report's entry per stage and whether the run is clean, with every reason it is not
QJsonObject report(const fs::path& dir, const QJsonObject& rec) {
    const std::string design = rec["design"].toString().toStdString(), platform = rec["platform"].toString().toStdString();
    const flow::Platform* pdk = flow::findPlatform(platform);
    if (!pdk) throw std::runtime_error("the run's platform '" + platform + "' is not known (known: " + flow::knownPlatforms() + ")");
    const fs::path tail = fs::path(platform) / design / "base", logs = dir / "logs" / tail, reports = dir / "reports" / tail,
                   results = dir / "results" / tail;
    const double t = pdk->nsPerTimeUnit;
    QJsonArray entries;
    QJsonArray notClean;
    if (rec["state"] != "done") notClean.append("the run is " + rec["state"].toString());
    const QJsonArray recorded = rec["stages"].toArray();
    auto add = [&](int stage, QJsonObject metrics, const QStringList& errors) {
        QJsonObject e{{"stage", kStages[stage]}, {"state", recorded[stage].toObject()["state"]}, {"metrics", metrics}};
        if (!errors.isEmpty()) {
            e["error"] = errors.join("; ");
            notClean.append(QString(kStages[stage]) + ": " + errors.join("; "));
        }
        entries.append(e);
    };
    auto physical = [&](const char* file, const char* prefix, QJsonObject& m, QStringList& errors) {
        readMetrics(logs / file, prefix, {{"instances", "design__instance__count", 1}, {"area_um2", "design__instance__area", 1},
                                          {"die_area_um2", "design__die__area", 1}, {"core_area_um2", "design__core__area", 1},
                                          {"utilization", "design__instance__utilization", 1},
                                          {"setup_slack_ns", "timing__setup__ws", t}, {"tns_ns", "timing__setup__tns", t},
                                          {"hold_slack_ns", "timing__hold__ws", t}, {"power_w", "power__total", 1},
                                          {"power_internal_w", "power__internal__total", 1},
                                          {"power_switching_w", "power__switching__total", 1},
                                          {"power_leakage_w", "power__leakage__total", 1}},
                    *pdk, m, errors);
        if (m.value("setup_slack_ns").isDouble()) m["wns_ns"] = std::min(0.0, m.value("setup_slack_ns").toDouble());
    };
    {
        QJsonObject m;
        QStringList errors;
        readMetrics(logs / "1_synth.json", "synth", {{"instances", "design__instance__count", 1}, {"area_um2", "design__instance__area", 1}},
                    *pdk, m, errors);
        add(0, m, errors);
    }
    for (const auto& [stage, file, prefix] : {std::tuple{1, "2_1_floorplan.json", "floorplan"}, {2, "3_5_place_dp.json", "detailedplace"},
                                              {3, "4_1_cts.json", "cts"}}) {
        QJsonObject m;
        QStringList errors;
        physical(file, prefix, m, errors);
        add(stage, m, errors);
    }
    {
        QJsonObject m;
        QStringList errors;
        physical("5_1_grt.json", "globalroute", m, errors);
        readMetrics(logs / "5_2_route.json", "detailedroute", {{"drc_errors", "route__drc_errors", 1}, {"antenna_nets", "antenna__violating__nets", 1},
                                                               {"wirelength_um", "route__wirelength", 1}, {"vias", "route__vias", 1}},
                    *pdk, m, errors);
        if (m.value("drc_errors").toDouble() > 0) notClean.append(QString::number(m.value("drc_errors").toDouble()) + " routing DRC errors");
        if (m.value("antenna_nets").toDouble() > 0) notClean.append(QString::number(m.value("antenna_nets").toDouble()) + " nets violate antenna rules");
        add(4, m, errors);
    }
    {
        QJsonObject m;
        QStringList errors;
        physical("6_report.json", "finish", m, errors);
        const fs::path gds = results / "6_final.gds";
        if (fs::exists(gds)) m["gds"] = q(gds.string());
        else errors.append("no GDS: " + q(gds.string()));
        if (m.value("wns_ns").toDouble() < 0) notClean.append("setup timing not met: WNS " + QString::number(m.value("wns_ns").toDouble()) + " ns");
        if (m.value("hold_slack_ns").toDouble() < 0) notClean.append("hold timing not met: slack " + QString::number(m.value("hold_slack_ns").toDouble()) + " ns");
        add(5, m, errors);
    }
    {
        QJsonObject m;
        QStringList errors;
        const DRCResult drc = readDrcReport((reports / "6_drc.lyrdb").string());
        for (const auto& e : drc.errors) errors.append(q(e));
        if (drc.success) {
            QJsonObject rules;
            for (const auto& v : drc.violations) rules[q(v.ruleName)] = v.count;
            m["violations"] = drc.totalViolations;
            m["rules"] = rules;
            if (drc.totalViolations > 0) notClean.append(QString::number(drc.totalViolations) + " DRC violations");
        }
        add(kDrc, m, errors);
    }
    {
        QJsonObject m;
        QStringList errors;
        if (pdk->lvsDeck.empty()) {
            errors.append(q(pdk->noLvs));
        } else {
            const LVSResult lvs = readLvsReport((results / "6_lvs.lvsdb").string());
            for (const auto& e : lvs.errors) errors.append(q(e));
            if (lvs.success) m = {{"matched", lvs.matched}, {"mismatches", lvs.mismatchCount}, {"devices_layout", lvs.deviceCountLayout},
                                  {"devices_schematic", lvs.deviceCountSchematic}, {"nets_layout", lvs.netCountLayout},
                                  {"nets_schematic", lvs.netCountSchematic}};
        }
        add(kLvs, m, errors);
    }
    return {{"run", rec["run"]}, {"design", rec["design"]}, {"platform", rec["platform"]}, {"state", rec["state"]}, {"stages", entries},
            {"clean", notClean.isEmpty()}, {"not_clean", notClean}};
}

// A tool's version line, or why there is none
void addVersion(QJsonObject& entry, const std::string& exe, const std::vector<std::string>& args) {
    acutesim::orchestration::Subprocess p(exe, args);
    p.setTimeout(std::chrono::seconds(10));
    const auto r = p.run();
    const QStringList lines = q(r.stdout_output + r.stderr_output).split('\n', Qt::SkipEmptyParts);
    if (r.exit_code == 0 && !lines.isEmpty()) entry["version"] = lines.first().trimmed();
    else entry["version_error"] = r.timed_out ? QString("timed out") : "exit " + QString::number(r.exit_code);
}

}  // namespace

void addFlowCommands(Registry& r) {
    r.add({"flow_tools", "Returns each tool a flow run needs: found or missing, its path and version, and the variable that overrides it.",
           object({}),
           [](Context&, const QJsonObject&) {
               const RtlFlowTools tools = RtlFlowTools::detect();
               const std::map<std::string, std::pair<const char*, std::vector<std::string>>> how = {
                   {"gmake", {"MAKE_EXE", {"--version"}}}, {"yosys", {"YOSYS_EXE", {"-V"}}},
                   {"openroad", {"OPENROAD_EXE", {"-version"}}}, {"klayout", {"KLAYOUT_CMD", {"-v"}}},
                   {"OpenROAD-flow-scripts", {"ORFS_FLOW_HOME", {}}}};
               QJsonArray out;
               for (const auto& [name, path] : tools.flowTools()) {
                   const auto& [env, args] = how.at(name);
                   QJsonObject e{{"name", q(name)}, {"found", !path.empty()}, {"path", q(path)}, {"override", env}};
                   if (const char* set = std::getenv(env)) e["override_value"] = set;
                   if (!path.empty() && !args.empty()) addVersion(e, path, args);
                   if (!path.empty() && args.empty()) addVersion(e, "git", {"-C", path, "rev-parse", "--short", "HEAD"});
                   out.append(e);
               }
               return QJsonValue(out);
           },
           true});
    r.add({"flow_platforms", "Returns the known flow platforms: for each whether it can run here, what is missing, its decks and time unit.",
           object({}),
           [](Context&, const QJsonObject&) {
               const RtlFlowTools tools = RtlFlowTools::detect();
               QJsonArray out;
               for (const auto& p : flow::platforms()) {
                   QJsonArray missing;
                   for (const auto& [name, path] : tools.flowTools())
                       if (path.empty()) missing.append(q(name));
                   if (!tools.flowDir.empty())
                       for (const auto& f : flow::missingFiles(p, tools.flowDir)) missing.append(q(f));
                   QJsonObject e{{"name", q(p.name)}, {"usable", missing.isEmpty()}, {"missing", missing}, {"time_unit", q(p.timeUnit)},
                                 {"drc_deck", q(p.drcDeck)}};
                   if (p.lvsDeck.empty()) {
                       e["lvs_deck"] = QJsonValue(QJsonValue::Null);
                       e["lvs_note"] = q(p.noLvs);
                   } else {
                       e["lvs_deck"] = q(p.lvsDeck);
                   }
                   out.append(e);
               }
               return QJsonValue(out);
           },
           true});
    r.add({"flow_run", "Runs Verilog to GDS on a platform (ORFS, then its DRC and LVS decks) as a new run under implementation/. Waits and "
                       "returns the run's status, or with wait false starts it in the background and returns its status at once (poll flow_status).",
           object({{"design", param("string", "the run's design name: letters, digits, _")},
                   {"verilog", QJsonObject{{"type", "array"}, {"description", "the Verilog sources: absolute paths"},
                                           {"items", QJsonObject{{"type", "string"}}}}},
                   {"top", param("string", "the top module")},
                   {"platform", param("string", "a platform of flow_platforms")},
                   {"clock_period_ns", param("number", "the clock's period in ns, on every platform")}},
                  {{"clock_port", param("string", "the clock's port (default clk)")},
                   {"core_utilization", param("number", "percent of the core the cells fill, within (0, 100) (default 40)")},
                   {"wait", param("boolean", "wait for the run (default true)")}}),
           [](Context& c, const QJsonObject& a) {
               RtlFlowConfig cfg;
               cfg.design = s(a, "design");
               cfg.top = s(a, "top");
               cfg.platform = s(a, "platform");
               cfg.clockPeriodNs = a["clock_period_ns"].toDouble();
               if (a.contains("clock_port")) cfg.clockPort = s(a, "clock_port");
               if (a.contains("core_utilization")) cfg.coreUtilization = a["core_utilization"].toDouble();
               for (const auto& v : a["verilog"].toArray()) cfg.verilog.push_back(v.toString().toStdString());
               const RtlFlowTools tools = RtlFlowTools::detect();
               cfg.workDir = (root(c) / "x").string();  // as the run's will be, for the check (it gets its name once the design's is known good)
               if (const std::string e = rtlFlowConfigError(cfg, tools); !e.empty()) throw std::invalid_argument(e);
               const flow::Platform& pdk = *flow::findPlatform(cfg.platform);

               const fs::path base = root(c) / cfg.design;
               fs::create_directories(base);
               int n = 0;
               for (const auto& e : fs::directory_iterator(base)) {
                   const std::string name = e.path().filename().string();
                   if (name.rfind("run.", 0) == 0) n = std::max(n, std::atoi(name.c_str() + 4));
               }
               while (!fs::create_directory(base / ("run." + std::to_string(++n)))) {}  // another process may take a number too
               const fs::path dir = base / ("run." + std::to_string(n));
               cfg.workDir = dir.string();

               QJsonArray stages, verilog;
               for (int i = 0; i < (int)std::size(kStages); ++i) {
                   QJsonObject st{{"name", kStages[i]}, {"state", "pending"}};
                   if (i == kLvs && pdk.lvsDeck.empty()) st = {{"name", kStages[i]}, {"state", "unavailable"}, {"note", q(pdk.noLvs)}};
                   stages.append(st);
               }
               for (const auto& v : cfg.verilog) verilog.append(q(v));
               const QJsonObject rec{{"run", q(cfg.design + "/run." + std::to_string(n))}, {"design", q(cfg.design)}, {"top", q(cfg.top)},
                                     {"platform", q(cfg.platform)}, {"verilog", verilog}, {"clock_period_ns", cfg.clockPeriodNs},
                                     {"clock_port", q(cfg.clockPort)}, {"core_utilization", cfg.coreUtilization}, {"dir", q(dir.string())},
                                     {"state", "running"}, {"stage", QJsonValue(QJsonValue::Null)}, {"stages", stages},
                                     {"pid", QCoreApplication::applicationPid()}, {"started", now()}};
               if (!writeRecord(dir, rec)) throw std::runtime_error("cannot write the run's record in " + dir.string());
               if (!a.contains("wait") || a["wait"].toBool()) {
                   execute(dir, rec, cfg, tools, nullptr);
                   return QJsonValue(shown(readRecord(dir)));
               }
               jobs(c).start(dir.string(), [dir, rec, cfg, tools](const std::atomic<bool>& cancel) { execute(dir, rec, cfg, tools, &cancel); });
               return QJsonValue(shown(rec));
           },
           true});
    r.add({"flow_status", "Returns a run's state (running, done, failed, stopped, interrupted), current stage, each stage's state and elapsed seconds, and its log's tail.",
           object({{"run", kRun}}, {{"tail", param("integer", "log lines to return (default 30)")}}),
           [](Context& c, const QJsonObject& a) {
               const int tail = a.contains("tail") ? a["tail"].toInt() : 30;
               if (tail < 0) throw std::invalid_argument("tail: a number of lines, 0 or more");
               const fs::path dir = runPath(c, a);
               QJsonObject out = shown(readRecord(dir));
               out["log_tail"] = logTail(dir / "flow.log", tail);
               return QJsonValue(out);
           },
           true});
    r.add({"flow_stop", "Stops a background run this session started, killing its tools' process group; the run ends as stopped (poll flow_status).",
           object({{"run", kRun}}),
           [](Context& c, const QJsonObject& a) {
               const fs::path dir = runPath(c, a);
               const QJsonObject rec = readRecord(dir);
               if (rec["state"] != "running") throw std::invalid_argument(s(a, "run") + " is not running (" + rec["state"].toString().toStdString() + ")");
               // ponytail: the cancel flag lives in the process running it; stopping another process's run needs a request on disk
               if (rec["pid"].toInteger() != QCoreApplication::applicationPid() || !jobs(c).cancel(dir.string()))
                   throw std::invalid_argument(s(a, "run") + " runs in process " + std::to_string(rec["pid"].toInteger()) + ", in the foreground or elsewhere: stop it there");
               return QJsonValue(QJsonObject{{"run", a["run"]}, {"state", "stopping"}});
           },
           true});
    r.add({"flow_list", "Lists the runs on disk, per design oldest first: each one's run, design, platform and state (or why its record cannot be read).",
           object({}, {{"design", param("string", "only this design's runs")}}),
           [](Context& c, const QJsonObject& a) {
               std::vector<std::pair<std::string, int>> runs;
               if (fs::is_directory(root(c)))
                   for (const auto& d : fs::directory_iterator(root(c))) {
                       const std::string design = d.path().filename().string();
                       if (!d.is_directory() || (a.contains("design") && design != s(a, "design"))) continue;
                       for (const auto& e : fs::directory_iterator(d.path())) {
                           const std::string name = e.path().filename().string();
                           if (e.is_directory() && name.rfind("run.", 0) == 0) runs.emplace_back(design, std::atoi(name.c_str() + 4));
                       }
                   }
               std::sort(runs.begin(), runs.end());
               QJsonArray out;
               for (const auto& [design, n] : runs) {
                   const QString run = q(design + "/run." + std::to_string(n));
                   try {
                       const QJsonObject rec = shown(readRecord(root(c) / design / ("run." + std::to_string(n))));
                       QJsonObject e{{"run", run}, {"design", rec["design"]}, {"platform", rec["platform"]}, {"state", rec["state"]}};
                       if (rec.contains("elapsed_s")) e["elapsed_s"] = rec["elapsed_s"];
                       out.append(e);
                   } catch (const std::exception& err) {
                       out.append(QJsonObject{{"run", run}, {"error", err.what()}});
                   }
               }
               return QJsonValue(out);
           },
           true});
    r.add({"flow_report", "Returns each stage's metrics from its reports (instances, area, utilization as a fraction, slack, WNS and TNS in ns, power in W, "
                          "routing, DRC and LVS counts); a missing report is an error in that stage's entry. clean is true only with every stage reported "
                          "and passing, and not_clean says why it is not.",
           object({{"run", kRun}}),
           [](Context& c, const QJsonObject& a) {
               const fs::path dir = runPath(c, a);
               return QJsonValue(report(dir, readRecord(dir)));
           },
           true});
}

}  // namespace cmd
