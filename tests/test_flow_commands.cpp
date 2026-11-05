// test_flow_commands: the flow's commands. With stand-in tools (a shell script as make, so it needs nothing installed):
// every malformed call refused; an unknown platform; tools an override leaves missing; a background run's status and
// stages, stopping it and its process group; a run whose reports are missing, as errors and never as zeros; list; a
// record whose process is gone; LVS and DRC reports read back. With the real tools (RtlFlowTools::detect(); skipped
// without them): gcd on asap7 from flow_run to flow_report, its LVS plainly unavailable, and a real run stopped.
#include "cmd/registry.h"
#include "flow/rtl_flow.h"
#include "oadb/oadb.h"

#include <QJsonDocument>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace acutesim::compute::orchestration;

static int g_fail = 0;
static void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++g_fail;
}

static bool waitFor(const std::function<bool()>& done, int seconds) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (!done()) {
        if (std::chrono::steady_clock::now() > end) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return true;
}

static QJsonObject stageOf(const QJsonObject& status, const char* name) {
    for (const auto& v : status["stages"].toArray())
        if (v.toObject()["name"] == name) return v.toObject();
    return {};
}
static QJsonObject reportStage(const QJsonObject& report, const char* name) {
    for (const auto& v : report["stages"].toArray())
        if (v.toObject()["stage"] == name) return v.toObject();
    return {};
}

static void writeExecutable(const fs::path& p, const std::string& text) {
    std::ofstream(p) << text;
    fs::permissions(p, fs::perms::owner_all);
}

static const char* const kOverrides[] = {"ORFS_FLOW_HOME", "MAKE_EXE", "YOSYS_EXE", "OPENROAD_EXE", "KLAYOUT_CMD"};

int main() {
    const fs::path dir = fs::temp_directory_path() / ("flow_cmds_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    auto ws = oadb::Workspace::load((dir / "w.libs").string());
    cmd::Registry reg;
    cmd::addFlowCommands(reg);
    auto ctx = std::make_unique<cmd::Context>(cmd::Context{ws, ""});
    auto call = [&](const char* name, const QJsonObject& args) { return reg.call(*ctx, name, args); };

    const fs::path src = dir / "gcd.v";
    std::ofstream(src) << "module gcd(input clk); endmodule\n";
    const QJsonObject good{{"design", "d"}, {"verilog", QJsonArray{QString::fromStdString(src.string())}}, {"top", "gcd"},
                           {"platform", "asap7"}, {"clock_period_ns", 1.0}, {"wait", false}};
    auto with = [&](const char* key, const QJsonValue& v) {
        QJsonObject a = good;
        a[key] = v;
        return a;
    };

    // ── stand-in tools: a platform tree with empty decks, and a make that prints ORFS's stage lines ──
    const fs::path bin = dir / "bin", orfs = dir / "orfs";
    fs::create_directories(bin);
    for (const auto& p : flow::platforms()) {
        std::vector<std::string> files{p.drcDeck, p.setRC};
        files.insert(files.end(), p.liberty.begin(), p.liberty.end());
        if (!p.lvsDeck.empty()) files.insert(files.end(), {p.lvsDeck, p.cdl});
        for (const auto& f : files) {
            const fs::path path = flow::platformFile(p, orfs.string(), f);
            fs::create_directories(path.parent_path());
            std::ofstream(path) << "";
        }
    }
    std::ofstream(orfs / "Makefile") << "";
    const fs::path pidFile = dir / "child.pid", fake = bin / "make";
    // Sleeps in a child, as make waits on a tool: only killing the group ends both. SLEEP absent: exits 0 with no reports.
    writeExecutable(fake, "#!/bin/sh\necho 'flow.sh 1_2_yosys'\necho 'flow.sh 3_3_place_gp'\n"
                          "if [ -e \"" + (dir / "sleep").string() + "\" ]; then sleep 300 & echo $! > \"" + pidFile.string() + "\"; wait; fi\nexit 0\n");
    writeExecutable(bin / "tool", "#!/bin/sh\necho fake 1.0\n");
    setenv("ORFS_FLOW_HOME", orfs.c_str(), 1);
    setenv("MAKE_EXE", fake.c_str(), 1);
    for (const char* e : {"YOSYS_EXE", "OPENROAD_EXE", "KLAYOUT_CMD"}) setenv(e, (bin / "tool").c_str(), 1);

    // ── refusals ──
    {
        auto refused = [&](const QJsonObject& a, const char* needle, const std::string& what) {
            const cmd::Result r = call("flow_run", a);
            check(!r.ok && r.error.contains(needle), "flow_run refuses " + what + (r.ok ? "" : " (" + r.error.toStdString() + ")"));
        };
        QJsonObject noTop = good;
        noTop.remove("top");
        refused(noTop, "top", "a missing parameter");
        refused(with("nonsense", 1), "nonsense", "an unknown parameter");
        refused(with("clock_period_ns", "fast"), "clock_period_ns", "a wrong type");
        refused(with("clock_period_ns", 0), "clock period", "a zero period");
        refused(with("clock_period_ns", -1), "clock period", "a negative period");
        refused(with("core_utilization", 100), "utilization", "a full core");
        refused(with("core_utilization", 0), "utilization", "an empty core");
        refused(with("design", "../up"), "design name", "a design name that leaves the tree");
        refused(with("top", "gcd\nexport X = 1"), "top module", "a top that would write make");
        refused(with("clock_port", "clk}] ; exit ;#"), "clock port", "a clock port that would write Tcl");
        refused(with("verilog", QJsonArray{}), "Verilog", "no sources");
        refused(with("verilog", QJsonArray{(dir / "absent.v").c_str()}), "absent.v", "a source that is not there");
        refused(with("verilog", QJsonArray{"gcd.v"}), "absolute", "a relative source");
        std::ofstream(dir / "a b.v") << "";
        refused(with("verilog", QJsonArray{(dir / "a b.v").c_str()}), "file name", "a source name with a space");
        const cmd::Result unknown = call("flow_run", with("platform", "sky130"));
        check(!unknown.ok && unknown.error.contains("sky130") && unknown.error.contains("ihp-sg13g2") && unknown.error.contains("asap7"),
              "an unknown platform is an error listing the known ones (" + unknown.error.toStdString() + ")");
        check(!fs::exists(dir / "implementation" / "d"), "nothing refused left a run behind");

        const fs::path spaced = dir / "with space";
        fs::create_directories(spaced);
        auto wsSpaced = oadb::Workspace::load((spaced / "w.libs").string());
        cmd::Context ctxSpaced{wsSpaced, ""};
        const cmd::Result sp = reg.call(ctxSpaced, "flow_run", good);
        check(!sp.ok && sp.error.contains("spaces"), "a workspace under a path with spaces is refused, not split by make");

        for (const char* c : {"flow_status", "flow_stop", "flow_report"}) {
            const cmd::Result traversal = call(c, {{"run", "../../etc/run.1"}}), absent = call(c, {{"run", "nope/run.1"}});
            check(!traversal.ok && traversal.error.contains("<design>/run.N") && !absent.ok && absent.error.contains("no run"),
                  std::string(c) + " refuses a malformed run name and one that does not exist");
        }
    }

    // ── tools an override leaves missing ──
    {
        setenv("OPENROAD_EXE", (dir / "nothing").c_str(), 1);
        setenv("ORFS_FLOW_HOME", (dir / "nothing").c_str(), 1);
        const cmd::Result tools = call("flow_tools", {});
        bool openroadMissing = false, orfsMissing = false, yosysFound = false;
        for (const auto& v : tools.value.toArray()) {
            const QJsonObject t = v.toObject();
            if (t["name"] == "openroad") openroadMissing = !t["found"].toBool() && t["path"] == "" && t["override_value"].toString().contains("nothing");
            if (t["name"] == "OpenROAD-flow-scripts") orfsMissing = !t["found"].toBool();
            if (t["name"] == "yosys") yosysFound = t["found"].toBool() && t["version"] == "fake 1.0";
        }
        check(tools.ok && openroadMissing && orfsMissing && yosysFound,
              "flow_tools: an override naming nothing leaves that tool missing, never another; others have path and version");
        const cmd::Result plats = call("flow_platforms", {});
        bool allUnusable = plats.ok && plats.value.toArray().size() == 2;
        for (const auto& v : plats.value.toArray())
            allUnusable = allUnusable && !v.toObject()["usable"].toBool() && v.toObject()["missing"].toArray().contains(QJsonValue("openroad"));
        check(allUnusable, "flow_platforms: both known platforms unusable, naming what is missing");
        const cmd::Result run = call("flow_run", good);
        check(!run.ok && run.error.contains("openroad") && run.error.contains("OpenROAD-flow-scripts"), "flow_run refuses, naming the missing tools");
        setenv("OPENROAD_EXE", (bin / "tool").c_str(), 1);
        setenv("ORFS_FLOW_HOME", orfs.c_str(), 1);
    }
    {
        const cmd::Result plats = call("flow_platforms", {});
        bool ihpOk = false, asap7Note = false;
        for (const auto& v : plats.value.toArray()) {
            const QJsonObject p = v.toObject();
            if (p["name"] == "ihp-sg13g2") ihpOk = p["usable"].toBool() && p["time_unit"] == "1ns" && p["lvs_deck"].isString();
            if (p["name"] == "asap7") asap7Note = p["usable"].toBool() && p["time_unit"] == "1ps" && p["lvs_deck"].isNull() && p["lvs_note"].toString().contains("no open LVS deck");
        }
        check(ihpOk && asap7Note, "flow_platforms: both usable with their files there; asap7 says it has no LVS deck");
        fs::remove(orfs / "platforms" / "asap7" / "setRC.tcl");
        const cmd::Result gap = call("flow_run", good);
        check(!gap.ok && gap.error.contains("setRC.tcl"), "a platform missing one of its files is refused, naming it");
        std::ofstream(orfs / "platforms" / "asap7" / "setRC.tcl") << "";
    }

    // ── a background run: status, stages, stop, its process group ──
    std::string stoppedRun;
    {
        std::ofstream(dir / "sleep") << "";
        const auto t0 = std::chrono::steady_clock::now();
        const cmd::Result started = call("flow_run", good);
        const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const QJsonObject st0 = started.value.toObject();
        stoppedRun = st0["run"].toString().toStdString();
        check(started.ok && stoppedRun == "d/run.1" && st0["state"] == "running" && took < 2.0,
              "wait false returns at once with its run id (" + std::to_string(took) + " s)");
        const QJsonObject runArg{{"run", QString::fromStdString(stoppedRun)}};
        check(waitFor([&] { return fs::exists(pidFile) && call("flow_status", runArg).value.toObject()["stage"] == "place"; }, 30), "flow_status follows the tool's output to its stage");
        const QJsonObject st = call("flow_status", runArg).value.toObject();
        check(st["state"] == "running" && stageOf(st, "synth")["state"] == "done" && stageOf(st, "floorplan")["state"] == "skipped" &&
                  stageOf(st, "place")["state"] == "running" && stageOf(st, "place")["elapsed_s"].isDouble() && stageOf(st, "route")["state"] == "pending" &&
                  stageOf(st, "lvs")["state"] == "unavailable" && stageOf(st, "lvs")["note"].toString().contains("no open LVS deck"),
              "stages: done, skipped (never seen), running with elapsed, pending; asap7's lvs unavailable from the start");
        bool tail = false;
        for (const auto& l : st["log_tail"].toArray()) tail = tail || l.toString().contains("flow.sh 3_3_place_gp");
        check(tail, "flow_status has the log's tail");
        check(!call("flow_status", {{"run", runArg["run"]}, {"tail", -1}}).ok, "a negative tail is refused");
        const QJsonObject partial = call("flow_report", runArg).value.toObject();
        check(!partial["clean"].toBool() && reportStage(partial, "synth")["error"].toString().contains("no report"), "flow_report on a running run: errors, not clean");

        pid_t child = 0;
        std::ifstream(pidFile) >> child;
        check(child > 0 && ::kill(child, 0) == 0, "the tool's child runs");
        const cmd::Result stop = call("flow_stop", runArg);
        check(stop.ok && stop.value.toObject()["state"] == "stopping", "flow_stop accepts it");
        check(waitFor([&] { return call("flow_status", runArg).value.toObject()["state"] == "stopped"; }, 10), "the run ends as stopped");
        const QJsonObject after = call("flow_status", runArg).value.toObject();
        check(stageOf(after, "place")["state"] == "stopped" && stageOf(after, "route")["state"] == "pending" && after["error"].toString().contains("cancelled"),
              "its stage says stopped, later ones never ran");
        check(waitFor([&] { return ::kill(child, 0) != 0; }, 5), "the whole process group is gone, the tool's child too");
        const cmd::Result again = call("flow_stop", runArg);
        check(!again.ok && again.error.contains("not running"), "stopping a stopped run is an error");
        fs::remove(dir / "sleep");
    }

    // ── a run whose tool leaves no reports: failed, and every report an error, never a zero ──
    {
        const cmd::Result done = call("flow_run", with("wait", true));
        const QJsonObject st = done.value.toObject();
        check(done.ok && st["run"] == "d/run.2" && st["state"] == "failed" && st["error"].toString().contains("6_finish.rpt") && stageOf(st, "place")["state"] == "failed",
              "wait true returns the status; no sign-off report: failed, naming it, in the stage it reached");
        const QJsonObject rep = call("flow_report", {{"run", "d/run.2"}}).value.toObject();
        bool allErrors = true, noZeros = true;
        for (const char* stage : {"synth", "floorplan", "place", "cts", "route", "finish", "drc", "lvs"}) {
            const QJsonObject e = reportStage(rep, stage);
            allErrors = allErrors && !e["error"].toString().isEmpty();
            noZeros = noZeros && e["metrics"].toObject().isEmpty();
        }
        check(allErrors && noZeros && !rep["clean"].toBool() && rep["not_clean"].toArray().size() >= 8,
              "flow_report: each missing report an error in its stage, no metric made up, not clean");
        check(reportStage(rep, "lvs")["error"].toString().contains("no open LVS deck") && reportStage(rep, "lvs")["state"] == "unavailable",
              "asap7's LVS: says no open deck exists");

        // One metric gone from a report that is there: the others read, that one is the error
        const fs::path logs = dir / "implementation/d/run.2/logs/asap7/d/base";
        fs::create_directories(logs);
        std::ofstream(logs / "1_synth.json") << R"({"synth__design__instance__count": 12})";
        std::ofstream(logs / "2_1_floorplan.json") << "{ not json";
        const QJsonObject rep2 = call("flow_report", {{"run", "d/run.2"}}).value.toObject();
        check(reportStage(rep2, "synth")["metrics"].toObject()["instances"].toDouble() == 12 && reportStage(rep2, "synth")["error"].toString().contains("instance__area") &&
                  !reportStage(rep2, "synth")["metrics"].toObject().contains("area_um2"),
              "a metric a report lacks is an error, not a zero");
        check(reportStage(rep2, "floorplan")["error"].toString().contains("malformed"), "a malformed report is an error");
        std::ofstream(logs / "1_synth.json") << R"({"run__flow__platform__time_units": "1ns", "synth__design__instance__count": 12, "synth__design__instance__area": 3})";
        check(reportStage(call("flow_report", {{"run", "d/run.2"}}).value.toObject(), "synth")["error"].toString().contains("1ns"),
              "a report in another time unit than the platform's is an error");
    }

    // ── the DRC and LVS readers ──
    {
        const fs::path f = dir / "r.lyrdb";
        check(!readDrcReport(f.string()).success, "DRC: no report is no result");
        std::ofstream(f) << "<report-database><categories><category><name>M1</name></category></categories><items>";
        check(!readDrcReport(f.string()).success, "DRC: a truncated report is no result");
        std::ofstream(f) << "<report-database><categories><category><name>M1</name></category></categories><items></items></report-database>";
        const DRCResult clean = readDrcReport(f.string());
        check(clean.success && clean.cleanDRC && clean.totalViolations == 0, "DRC: no items: clean, the rule list's categories not counted");
        std::ofstream(f) << "<report-database><items><item><category>'M1.W'</category></item><item><category>M2.S</category></item>"
                            "<item><category>'M1.W'</category></item></items></report-database>";
        const DRCResult dirty = readDrcReport(f.string());
        check(dirty.success && !dirty.cleanDRC && dirty.totalViolations == 3 && dirty.violations.size() == 2 && dirty.violations[0].ruleName == "M1.W" && dirty.violations[0].count == 2,
              "DRC: items counted per rule");
        std::ofstream(f) << "<report-database><categories><category><name>  </name><description>M1.S.2 : spacing</description></category></categories>"
                            "<items><item><category>'  '</category></item></items></report-database>";
        check(readDrcReport(f.string()).violations.at(0).ruleName == "M1.S.2", "DRC: a rule the deck left unnamed goes by its description's name");

        const fs::path g = dir / "r.lvsdb";
        check(!readLvsReport(g.string()).success, "LVS: no report is no result");
        std::ofstream(g) << "#%lvsdb-klayout\nJ(\n X(1 C I(a)\n )\n)\n";
        check(!readLvsReport(g.string()).success, "LVS: no cross-reference: nothing was compared, no result");
        std::ofstream(g) << "J(\n)\nZ(\n X(top TOP 1\n  Z(\n   N(1 2 1)\n   N(2 1 1)\n   D(1 1 1)\n  )\n )\n)\n";
        const LVSResult match = readLvsReport(g.string());
        check(match.success && match.matched && match.netCountLayout == 2 && match.deviceCountSchematic == 1 && match.mismatchCount == 0, "LVS: all matched, counted");
        std::ofstream(g) << "Z(\n X(top TOP 0\n  Z(\n   N(1 () 0)\n   N(() 1 0)\n   D(1 1 1)\n   D(() 2 0)\n  )\n )\n)\n";
        const LVSResult differ = readLvsReport(g.string());
        check(differ.success && !differ.matched && differ.mismatchCount == 4 && differ.netCountLayout == 1 && differ.netCountSchematic == 1 && differ.deviceCountLayout == 1 && differ.deviceCountSchematic == 2,
              "LVS: a mismatch and a net with no counterpart are not a match");
    }

    // ── list; a record whose process is gone; a record that cannot be read ──
    {
        const pid_t gone = fork();
        if (gone == 0) _exit(0);
        waitpid(gone, nullptr, 0);
        const fs::path dead = dir / "implementation/d/run.3", bad = dir / "implementation/d/run.4";
        fs::create_directories(dead);
        fs::create_directories(bad);
        std::ofstream(dead / "run.json") << R"({"run":"d/run.3","design":"d","platform":"asap7","state":"running","pid":)" << gone
                                         << R"(,"started":1,"stage":"place","stages":[{"name":"place","state":"running","started":1}]})";
        std::ofstream(bad / "run.json") << "{";
        const QJsonObject st = call("flow_status", {{"run", "d/run.3"}}).value.toObject();
        check(st["state"] == "interrupted" && st["error"].toString().contains("gone") && stageOf(st, "place")["state"] == "interrupted",
              "a run whose process is gone says interrupted, not running");
        check(!call("flow_stop", {{"run", "d/run.3"}}).ok, "and it cannot be stopped");
        check(!call("flow_status", {{"run", "d/run.4"}}).ok, "a malformed record is an error");
        const QJsonArray list = call("flow_list", {}).value.toArray();
        check(list.size() == 4 && list[0].toObject()["run"] == "d/run.1" && list[0].toObject()["state"] == "stopped" && list[1].toObject()["state"] == "failed" &&
                  list[2].toObject()["state"] == "interrupted" && list[3].toObject().contains("error") && list[0].toObject()["platform"] == "asap7",
              "flow_list: the runs on disk in order, one it cannot read saying so");
        check(call("flow_list", {{"design", "other"}}).value.toArray().isEmpty() && call("flow_list", {{"design", "d"}}).value.toArray().size() == 4, "flow_list by design");
    }

    // ── runs outlive the session that ran them, and a session's end stops its own ──
    {
        std::ofstream(dir / "sleep") << "";
        fs::remove(pidFile);
        const std::string run = call("flow_run", good).value.toObject()["run"].toString().toStdString();
        waitFor([&] { return fs::exists(pidFile); }, 30);
        ctx = std::make_unique<cmd::Context>(cmd::Context{ws, ""});  // the old one's Jobs stop and join its runs
        const QJsonObject st = call("flow_status", {{"run", QString::fromStdString(run)}}).value.toObject();
        check(st["state"] == "stopped", "a session's end stops its background run, and the next session reads its record");
        fs::remove(dir / "sleep");
    }

    for (const char* e : kOverrides) unsetenv(e);
    const RtlFlowTools tools = RtlFlowTools::detect();
    if (!tools.missing().empty()) {
        std::printf("RTL flow tools not found (%s): real runs skipped\n", tools.missing().c_str());
    } else {
        const QJsonObject gcd{{"design", "gcd"}, {"verilog", QJsonArray{QString::fromStdString((fs::path(tools.flowDir) / "designs/src/gcd/gcd.v").string())}},
                              {"top", "gcd"}};
        // ── a real run stopped: nothing of it left running ──
        {
            QJsonObject a = gcd;
            a["platform"] = "ihp-sg13g2";
            a["clock_period_ns"] = 10.0;
            a["wait"] = false;
            const QString run = call("flow_run", a).value.toObject()["run"].toString();
            const QJsonObject arg{{"run", run}};
            check(waitFor([&] { return !stageOf(call("flow_status", arg).value.toObject(), "floorplan")["state"].toString().contains("pending"); }, 120), "a real run reaches floorplan");
            check(call("flow_stop", arg).ok && waitFor([&] { return call("flow_status", arg).value.toObject()["state"] == "stopped"; }, 15), "stopped");
            const std::string pgrep = "pgrep -f '" + (dir / "implementation").string() + "/" + run.toStdString() + "' > /dev/null";
            check(waitFor([&] { return std::system(pgrep.c_str()) != 0; }, 10), "no process of that run is left");
        }
        // ── gcd on asap7, end to end ──
        {
            QJsonObject a = gcd;
            a["platform"] = "asap7";
            a["clock_period_ns"] = 0.4;
            a["wait"] = false;
            const QJsonObject start = call("flow_run", a).value.toObject();
            const QJsonObject arg{{"run", start["run"]}};
            check(start["state"] == "running", "asap7 gcd started");
            check(waitFor([&] { return call("flow_status", arg).value.toObject()["state"] != "running"; }, 1200), "and ended");
            const QJsonObject st = call("flow_status", {{"run", start["run"]}, {"tail", 15}}).value.toObject();
            if (st["state"] != "done")
                for (const auto& l : st["log_tail"].toArray()) std::printf("  | %s\n", l.toString().toStdString().c_str());
            check(st["state"] == "done", "asap7 gcd ran through DRC: " + st["state"].toString().toStdString() + " " + st["error"].toString().toStdString());
            const QJsonObject rep = call("flow_report", arg).value.toObject();
            std::printf("  report: %s\n", QJsonDocument(rep).toJson(QJsonDocument::Compact).constData());
            for (const char* stage : {"synth", "floorplan", "place", "cts", "route", "finish", "drc"})
                check(!reportStage(rep, stage).contains("error"), std::string(stage) + " reported " + reportStage(rep, stage)["error"].toString().toStdString());
            const QJsonObject finish = reportStage(rep, "finish")["metrics"].toObject();
            // gcd's real size and speed: tens of um^2 in ASAP7, slack a fraction of 0.4 ns (a wrong time unit would be off by 1000)
            check(finish["area_um2"].toDouble() > 10 && finish["area_um2"].toDouble() < 200 && finish["utilization"].toDouble() > 0.1 && finish["utilization"].toDouble() < 1, "asap7: area and utilization plausible");
            check(std::abs(finish["setup_slack_ns"].toDouble()) < 0.4, "asap7: the slack in ns, not ps");
            check(finish["power_w"].toDouble() > 1e-6 && finish["power_w"].toDouble() < 1e-1, "asap7: power in W");
            const QJsonObject lvs = reportStage(rep, "lvs");
            check(lvs["state"] == "unavailable" && lvs["error"].toString().contains("no open LVS deck") && !rep["clean"].toBool(), "asap7: LVS plainly unavailable, so never clean");
            std::string err;
            const fs::path results = fs::path(st["dir"].toString().toStdString()) / "results/asap7/gcd/base";
            const double fmin = staMinPeriodNs(results.string(), "asap7", tools, 0.0, err);
            check(fmin > 0.05 && fmin < 1.0, "staMinPeriodNs on asap7's Liberty and setRC: " + std::to_string(fmin) + " ns " + err);
        }
    }

    ctx.reset();
    fs::remove_all(dir);
    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
