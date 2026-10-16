#include "rtl_flow.h"
#include "../server/subprocess.h"

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <thread>

namespace acutesim::compute::orchestration {

namespace fs = std::filesystem;
using acutesim::orchestration::Subprocess;

namespace {

bool executable(const std::string& p) { return !p.empty() && access(p.c_str(), X_OK) == 0; }

std::string onPath(const std::string& name) {
    std::stringstream path(std::getenv("PATH") ? std::getenv("PATH") : "");
    for (std::string dir; std::getline(path, dir, ':');)
        if (executable(dir + "/" + name)) return dir + "/" + name;
    return "";
}

std::string slurp(const fs::path& p) {
    std::ifstream f(p);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Last capture of `re` in `text`, or "" when it never matches
std::string lastMatch(const std::string& text, const std::regex& re, int group = 1) {
    std::string found;
    for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end; ++it) found = (*it)[group];
    return found;
}

double num(const std::string& s, double fallback = 0.0) {
    try { return s.empty() ? fallback : std::stod(s); } catch (...) { return fallback; }
}

}  // namespace

RtlFlowTools RtlFlowTools::detect() {
    RtlFlowTools t;
    const std::string eda = std::string(std::getenv("HOME") ? std::getenv("HOME") : "") + "/eda";
    auto pick = [](const char* env, const std::string& built, const char* name) -> std::string {
        if (const char* e = std::getenv(env); e && executable(e)) return e;
        if (executable(built)) return built;
        return onPath(name);
    };
    const char* flow = std::getenv("ORFS_FLOW_HOME");
    t.flowDir = flow ? flow : eda + "/orfs/flow";
    if (!fs::exists(fs::path(t.flowDir) / "Makefile")) t.flowDir.clear();
    t.make = pick("MAKE_EXE", "", "gmake");  // ORFS needs GNU make 4; macOS's make is 3.81
    t.yosys = pick("YOSYS_EXE", "", "yosys");
    t.openroad = pick("OPENROAD_EXE", eda + "/bin/openroad", "openroad");
    t.klayout = pick("KLAYOUT_CMD", eda + "/bin/klayout", "klayout");
    if (executable(eda + "/bin/pmap")) t.binDir = eda + "/bin";
    return t;
}

std::string RtlFlowTools::missing() const {
    std::string m;
    for (const auto& [name, path] : {std::pair{"OpenROAD-flow-scripts", flowDir}, {"gmake", make},
                                     {"yosys", yosys}, {"openroad", openroad}, {"klayout", klayout}})
        if (path.empty()) m += (m.empty() ? "" : ", ") + std::string(name);
    return m;
}

RtlFlowResult runRtlToGds(const RtlFlowConfig& cfg, const RtlFlowTools& tools, FlowLog log, FlowStage stage,
                          const std::atomic<bool>* cancel) {
    const auto t0 = std::chrono::steady_clock::now();
    RtlFlowResult res;
    auto fail = [&](const std::string& e) {
        res.error = e;
        res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (log) log(e, true);
        return res;
    };
    if (const std::string m = tools.missing(); !m.empty()) return fail("RTL flow: missing " + m);
    if (cfg.platform != "ihp-sg13g2") return fail("RTL flow: only ihp-sg13g2 is wired (its DRC and LVS decks)");
    if (cfg.workDir.empty() || cfg.workDir.find_first_of(" \t") != std::string::npos)
        return fail("RTL flow: the work directory must be set and contain no spaces (make splits paths on them)");
    if (cfg.top.empty() || cfg.verilog.empty()) return fail("RTL flow: a top module and Verilog sources are required");
    if (!std::regex_match(cfg.design, std::regex("[A-Za-z0-9_]+")))
        return fail("RTL flow: the design name may use only letters, digits and _");
    if (!(cfg.clockPeriodNs > 0.0) || !(cfg.coreUtilization > 0.0 && cfg.coreUtilization < 100.0))
        return fail("RTL flow: the clock period must be > 0 and the utilization within (0, 100) %");

    // Sources are copied in, so their own paths may hold spaces
    const fs::path work = fs::absolute(cfg.workDir), src = work / "src" / cfg.design;
    std::error_code ec;
    fs::create_directories(src, ec);
    std::string verilog;
    std::set<std::string> names;
    for (const auto& v : cfg.verilog) {
        const std::string name = fs::path(v).filename().string();
        if (!names.insert(name).second) return fail("RTL flow: two sources are named " + name);
        fs::copy_file(v, src / name, fs::copy_options::overwrite_existing, ec);
        if (ec) return fail("RTL flow: cannot copy " + v + ": " + ec.message());
        verilog += " " + (src / name).string();
    }
    const fs::path sdc = src / "constraint.sdc", mk = src / "config.mk";
    std::ofstream(sdc) << "create_clock -name core_clock -period " << cfg.clockPeriodNs << " [get_ports {" << cfg.clockPort
                       << "}]\n"
                          "set non_clock_inputs [delete_from_list [all_inputs] [get_ports {" << cfg.clockPort << "}]]\n"
                          "set_input_delay " << 0.2 * cfg.clockPeriodNs << " -clock core_clock $non_clock_inputs\n"
                          "set_output_delay " << 0.2 * cfg.clockPeriodNs << " -clock core_clock [all_outputs]\n";
    std::ofstream(mk) << "export DESIGN_NICKNAME = " << cfg.design << "\nexport DESIGN_NAME = " << cfg.top
                      << "\nexport PLATFORM = " << cfg.platform << "\nexport VERILOG_FILES =" << verilog
                      << "\nexport SDC_FILE = " << sdc.string() << "\nexport CORE_UTILIZATION = " << cfg.coreUtilization
                      << "\nexport PLACE_DENSITY_LB_ADDON = 0.2\nexport TNS_END_PERCENT = 100\nexport USE_FILL = 1\n";

    const std::string threads = std::to_string(std::max(1, (int)std::thread::hardware_concurrency() - 2));
    const char* path = std::getenv("PATH");
    const std::map<std::string, std::string> env{
        {"PATH", (tools.binDir.empty() ? "" : tools.binDir + ":") + (path ? path : "")}};
    // Runs one tool, streaming whole lines to the log; false when it failed or was cancelled
    auto runTool = [&](const std::string& exe, const std::vector<std::string>& args, std::string* output = nullptr) {
        Subprocess p(exe, args);
        p.setWorkingDirectory(work.string());
        p.setEnvironment(env);
        p.setTimeout(std::chrono::hours(4));
        p.setCancelFlag(cancel);
        std::string pending;
        auto onChunk = [&](const std::string& chunk) {
            pending += chunk;
            for (size_t nl; (nl = pending.find('\n')) != std::string::npos; pending.erase(0, nl + 1)) {
                const std::string line = pending.substr(0, nl);
                if (log) log(line, false);
                static const std::regex stageRe(R"(flow\.sh ([1-6])_(\S+))");
                std::smatch m;
                if (stage && std::regex_search(line, m, stageRe)) {
                    static const double at[] = {0, 0.05, 0.15, 0.25, 0.40, 0.50, 0.80};
                    stage(m[1].str() + "_" + m[2].str(), at[m[1].str()[0] - '0']);
                }
            }
        };
        const auto r = p.runWithStreaming(onChunk, onChunk);
        if (output) *output = r.stdout_output + r.stderr_output;
        if (r.cancelled) res.error = "cancelled";
        else if (r.timed_out) res.error = exe + " timed out";
        else if (r.exit_code != 0) res.error = fs::path(exe).filename().string() + " failed (exit " + std::to_string(r.exit_code) + ")";
        return r.exit_code == 0 && !r.cancelled && !r.timed_out;
    };

    // Synthesis -> floorplan -> PDN -> placement -> CTS -> routing -> fill -> GDS (ORFS "finish")
    if (stage) stage("synthesis", 0.0);
    if (!runTool(tools.make, {"-C", tools.flowDir, "DESIGN_CONFIG=" + mk.string(), "WORK_HOME=" + work.string(),
                              "YOSYS_EXE=" + tools.yosys, "OPENROAD_EXE=" + tools.openroad, "KLAYOUT_CMD=" + tools.klayout,
                              "NUM_CORES=" + threads, "finish"}))
        return fail("RTL flow: " + res.error);

    const fs::path tail = fs::path(cfg.platform) / cfg.design / "base";
    const fs::path results = work / "results" / tail, reports = work / "reports" / tail, logs = work / "logs" / tail;
    const fs::path platform = fs::path(tools.flowDir) / "platforms" / cfg.platform;
    res.gds = (results / "6_final.gds").string();
    res.reportDir = reports.string();

    const std::string stat = slurp(reports / "synth_stat.txt");
    // Hierarchical designs list every module and the top last ("for top module"), flat ones one
    res.synth.estimatedArea = num(lastMatch(stat, std::regex(R"(Chip area for (?:top )?module '[^']*': ([0-9.eE+-]+))")));
    res.synth.cellCount = (int)num(lastMatch(stat, std::regex(R"(\n\s*(\d+)\s+\S+\s+\d+\s+\S+\s+cells\n)")));
    res.synth.success = res.synth.cellCount > 0;

    const std::string finish = slurp(reports / "6_finish.rpt"), report = slurp(logs / "6_report.log");
    auto& pnr = res.pnr;
    pnr.timing.wns = num(lastMatch(finish, std::regex(R"(\nwns max ([0-9.eE+-]+))")));
    pnr.timing.tns = num(lastMatch(finish, std::regex(R"(\ntns max ([0-9.eE+-]+))")));
    pnr.timing.timingClean = pnr.timing.wns >= 0.0;
    // The PnR dialog's units: power in mW, utilization as a fraction
    static const std::regex powerRe(R"(\nTotal\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+))");
    if (std::smatch m; std::regex_search(finish, m, powerRe)) {
        pnr.power.internalPower = 1e3 * num(m[1]);
        pnr.power.dynamicPower = 1e3 * (num(m[1]) + num(m[2]));
        pnr.power.leakagePower = 1e3 * num(m[3]);
        pnr.power.totalPower = 1e3 * num(m[4]);
    }
    pnr.placement.totalArea = num(lastMatch(report, std::regex(R"(Design area ([0-9.]+) um\^2)")));
    pnr.placement.utilization = num(lastMatch(report, std::regex(R"(Design area [0-9.]+ um\^2 ([0-9.]+)% utilization)"))) / 100.0;
    pnr.placement.instanceCount = res.synth.cellCount;
    const std::string routeDrc = slurp(reports / "5_route_drc.rpt");
    for (size_t at = 0; (at = routeDrc.find("violation type", at)) != std::string::npos; ++at) ++pnr.routing.drcViolations;
    pnr.routing.antennaViolations = (int)num(lastMatch(slurp(reports / "drt_antennas.log"), std::regex(R"(Found (\d+) net violations)")));
    pnr.routing.drcClean = pnr.routing.drcViolations == 0 && pnr.routing.antennaViolations == 0;
    pnr.success = fs::exists(res.gds);
    if (!pnr.success) return fail("RTL flow: no GDS was written");

    // DRC: IHP's full ("maximal") deck. Its global density rules are chip-level (met by IHP's
    // filler inside the seal ring, which keeps metal fill off transistors), so a block runs
    // without them; every other rule, off-grid, filler and recommended rules included, applies.
    if (stage) stage("DRC", 0.88);
    std::string drcLog;
    if (!runTool(tools.klayout, {"-b", "-rd", "in_gds=" + res.gds, "-rd", "report_file=" + (reports / "6_drc_maximal.lyrdb").string(),
                                 "-rd", "densityRules=false", "-rd", "threads=" + threads, "-r",
                                 (platform / "drc" / "sg13g2_maximal.lydrc").string()},
                 &drcLog))
        return fail("RTL flow: DRC " + res.error);
    static const std::regex ruleRe(R"((?:^|\n)([A-Za-z0-9_./]+): (\d+)(?=\n))");
    for (std::sregex_iterator it(drcLog.begin(), drcLog.end(), ruleRe), end; it != end; ++it)
        if (const int n = std::stoi((*it)[2]); n > 0) {
            DRCViolation v;
            v.ruleName = (*it)[1];
            v.count = n;
            v.severity = "error";
            res.drc.violations.push_back(v);
            res.drc.totalViolations += n;
        }
    res.drc.errorCount = res.drc.totalViolations;
    res.drc.success = drcLog.find("Number of DRC errors") != std::string::npos;
    res.drc.cleanDRC = res.drc.success && res.drc.totalViolations == 0;

    // LVS: the netlist must include the decap and fill cells (physical-only, but decaps hold
    // transistors between VDD and VSS), then IHP's KLayout LVS deck
    if (stage) stage("LVS", 0.95);
    const fs::path cdl = results / "6_lvs.cdl", cdlTcl = results / "6_lvs_cdl.tcl";
    std::ofstream(cdlTcl) << "read_db " << (results / "6_final.odb").string() << "\nwrite_cdl -include_fillers -masters "
                          << (platform / "cdl" / "sg13g2_stdcell.cdl").string() << " " << cdl.string() << "\n";
    if (!runTool(tools.openroad, {"-no_init", "-exit", cdlTcl.string()})) return fail("RTL flow: CDL netlist " + res.error);
    std::ofstream(cdl, std::ios::app) << slurp(platform / "cdl" / "sg13g2_stdcell.cdl");
    // IHP's deck extracts an antenna cell's diodes in the parent (the cell's wells merge with
    // its row's), so the layout cell comes out empty, simplify purges it after align, and the
    // schematic's antenna instances are left without a counterpart. A copy of the deck
    // flattens the antenna cells on both sides before align: their diodes are still compared,
    // one level up.
    const fs::path lvsDir = platform / "lvs", deck = results / "6_lvs.lvs";
    std::string deckText = slurp(lvsDir / "sg13g2.lvs");
    const std::string inc = "# %include rule_decks/", align = "\nalign\n";
    if (deckText.find(inc) == std::string::npos || deckText.find(align) == std::string::npos)
        return fail("RTL flow: IHP's LVS deck changed (no rule_decks includes or align): update rtl_flow.cpp");
    for (size_t at = 0; (at = deckText.find(inc, at)) != std::string::npos; at += inc.size())
        deckText.replace(at, inc.size(), "# %include " + (lvsDir / "rule_decks").string() + "/");
    deckText.replace(deckText.find(align), align.size(),
                     "\nnetlist.flatten_circuit(\"sg13g2_antenna*\")\nschematic.flatten_circuit(\"sg13g2_antenna*\")\nalign\n");
    std::ofstream(deck) << deckText;
    std::string lvsLog;
    if (!runTool(tools.klayout, {"-b", "-rd", "input=" + res.gds, "-rd", "topcell=" + cfg.top, "-rd", "schematic=" + cdl.string(),
                                 "-rd", "report=" + (results / "6_lvs.lvsdb").string(), "-rd",
                                 "target_netlist=" + (results / "6_lvs_extracted.cir").string(), "-rd", "run_mode=deep", "-r",
                                 deck.string()},
                 &lvsLog))
        return fail("RTL flow: LVS " + res.error);
    res.lvs.success = lvsLog.find("Netlists") != std::string::npos;
    res.lvs.matched = lvsLog.find("Netlists match") != std::string::npos;
    if (!res.lvs.matched) res.lvs.errors.push_back("layout and schematic netlists do not match (" + (results / "6_lvs.lvsdb").string() + ")");

    res.success = pnr.timing.timingClean && pnr.routing.drcClean && res.drc.cleanDRC && res.lvs.matched;
    res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (stage) stage("done", 1.0);
    return res;
}

}  // namespace acutesim::compute::orchestration
