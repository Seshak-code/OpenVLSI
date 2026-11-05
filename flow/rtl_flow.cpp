#include <functional>
#include <cstdint>
#include "rtl_flow.h"
#include "subprocess.h"

#ifndef _WIN32
#include <unistd.h>
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

namespace flow {

const std::vector<Platform>& platforms() {
    static const std::vector<Platform> all = {
        {"ihp-sg13g2", "1ns", 1.0, "drc/sg13g2_maximal.lydrc",
         // its global density rules are chip-level (met by IHP's filler inside the seal ring, which keeps metal fill off
         // transistors), so a block runs without them; every other rule, off-grid, filler and recommended rules included, applies
         {"densityRules=false"},
         "lvs/sg13g2.lvs", "", "cdl/sg13g2_stdcell.cdl", "sg13g2_antenna*", {"lib/sg13g2_stdcell_typ_1p20V_25C.lib"}, "setRC.tcl",
         {"USE_FILL = 1"},
         {"~/eda/ihp-verilog/sg13g2_stdcell.v", "~/eda/ihp-verilog/sg13g2_udp.v"}, true, "sg13g2.lyp", "sg13g2"},
        {"asap7", "1ps", 1e-3, "drc/asap7.lydrc", {},
         "", "no open LVS deck exists for asap7 (ORFS, the PDK and lambdapdk have none; the PDK's own is Calibre)", "", "",
         {"lib/NLDM/asap7sc7p5t_AO_RVT_FF_nldm_211120.lib.gz", "lib/NLDM/asap7sc7p5t_INVBUF_RVT_FF_nldm_220122.lib.gz",
          "lib/NLDM/asap7sc7p5t_OA_RVT_FF_nldm_211120.lib.gz", "lib/NLDM/asap7sc7p5t_SIMPLE_RVT_FF_nldm_211120.lib.gz",
          "lib/NLDM/asap7sc7p5t_SEQ_RVT_FF_nldm_220123.lib"},
         "setRC.tcl", {},
         {"verilog/stdcell/asap7sc7p5t_AO_RVT_TT_201020.v", "verilog/stdcell/asap7sc7p5t_INVBUF_RVT_TT_201020.v",
          "verilog/stdcell/asap7sc7p5t_OA_RVT_TT_201020.v", "verilog/stdcell/asap7sc7p5t_SIMPLE_RVT_TT_201020.v",
          "verilog/stdcell/asap7sc7p5t_SEQ_RVT_TT_220101.v"}, false, "KLayout/asap7.lyp", ""},
    };
    return all;
}

const Platform* findPlatform(const std::string& name) {
    for (const auto& p : platforms())
        if (p.name == name) return &p;
    return nullptr;
}

std::string knownPlatforms() {
    std::string names;
    for (const auto& p : platforms()) names += (names.empty() ? "" : ", ") + p.name;
    return names;
}

std::string platformFile(const Platform& p, const std::string& flowDir, const std::string& file) {
    if (file.rfind("~/", 0) == 0) return std::string(std::getenv("HOME") ? std::getenv("HOME") : "") + file.substr(1);
    return (fs::path(flowDir) / "platforms" / p.name / file).string();
}

std::vector<std::string> missingFiles(const Platform& p, const std::string& flowDir) {
    std::vector<std::string> needed{p.drcDeck, p.setRC}, out;
    needed.insert(needed.end(), p.liberty.begin(), p.liberty.end());
    if (!p.lvsDeck.empty()) {
        needed.push_back(p.lvsDeck);
        needed.push_back(p.cdl);
    }
    for (const auto& f : needed)
        if (const std::string path = platformFile(p, flowDir, f); !fs::exists(path)) out.push_back(path);
    return out;
}

}  // namespace flow

namespace acutesim::compute::orchestration {

using acutesim::orchestration::Subprocess;

namespace {

#ifdef _WIN32
constexpr char kPathSep = ';';
bool executable(const std::string& p) { std::error_code ec; return !p.empty() && fs::is_regular_file(p, ec); }
#else
constexpr char kPathSep = ':';
bool executable(const std::string& p) { return !p.empty() && access(p.c_str(), X_OK) == 0; }
#endif

std::string onPath(const std::string& name) {
    std::stringstream path(std::getenv("PATH") ? std::getenv("PATH") : "");
    for (std::string dir; std::getline(path, dir, kPathSep);) {
        if (executable(dir + "/" + name)) return dir + "/" + name;
#ifdef _WIN32
        if (executable(dir + "/" + name + ".exe")) return dir + "/" + name + ".exe";
#endif
    }
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

// Runs one tool in `work` (tools.binDir first on PATH), handing every output line to onLine;
// false, with `error` set, when it failed, timed out or was cancelled
bool run(const RtlFlowTools& tools, const fs::path& work, const std::string& exe, const std::vector<std::string>& args,
         const std::atomic<bool>* cancel, std::string& error, std::string* output,
         const std::function<void(const std::string&)>& onLine) {
    Subprocess p(exe, args);
    p.setWorkingDirectory(work.string());
    const char* path = std::getenv("PATH");
    p.setEnvironment({{"PATH", (tools.binDir.empty() ? "" : tools.binDir + kPathSep) + (path ? path : "")}});
    p.setTimeout(std::chrono::hours(4));
    p.setCancelFlag(cancel);
    std::string pending;
    auto onChunk = [&](const std::string& chunk) {
        pending += chunk;
        for (size_t nl; (nl = pending.find('\n')) != std::string::npos; pending.erase(0, nl + 1))
            if (onLine) onLine(pending.substr(0, nl));
    };
    const auto r = p.runWithStreaming(onChunk, onChunk);
    if (output) *output = r.stdout_output + r.stderr_output;
    if (r.cancelled) error = "cancelled";
    else if (r.timed_out) error = exe + " timed out";
    else if (r.exit_code != 0) error = fs::path(exe).filename().string() + " failed (exit " + std::to_string(r.exit_code) + ")";
    return r.exit_code == 0 && !r.cancelled && !r.timed_out;
}

// Escaped Verilog identifiers (\a.b[0]$x) to plain ones (a_b_0__x): Icarus splits them on '.'
// when it annotates SDF. The SDF's escaped characters (a\.b\[0\]\$x) map to the same name.
std::string plainNames(const std::string& v) {
    std::string out;
    out.reserve(v.size());
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] != '\\') { out += v[i]; continue; }
        for (++i; i < v.size() && !std::isspace((unsigned char)v[i]); ++i) out += std::isalnum((unsigned char)v[i]) ? v[i] : '_';
        --i;
    }
    return out;
}

// `wire delayed_A, delayed_B;` -> the wires plus `assign delayed_A = A; ...`
std::string aliasDelayed(const std::string& cells) {
    static const std::regex declRe(R"(wire (delayed_[\w, ]*);)");
    std::string out;
    auto it = cells.cbegin();
    for (std::sregex_iterator m(cells.begin(), cells.end(), declRe), end; m != end; ++m) {
        out.append(it, (*m)[0].second);
        std::stringstream names((*m)[1].str());
        for (std::string n; std::getline(names, n, ',');) {
            n.erase(std::remove_if(n.begin(), n.end(), ::isspace), n.end());
            out += " assign " + n + " = " + n.substr(8) + ";";
        }
        it = (*m)[0].second;
    }
    out.append(it, cells.cend());
    return out;
}

}  // namespace

RtlFlowTools RtlFlowTools::detect() {
    RtlFlowTools t;
    const std::string eda = std::string(std::getenv("HOME") ? std::getenv("HOME") : "") + "/eda";
    auto pick = [](const char* env, const std::string& built, const char* name) -> std::string {
        if (const char* e = std::getenv(env)) return executable(e) ? e : "";  // never another tool than the one asked for
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
    t.iverilog = pick("IVERILOG_EXE", "", "iverilog");
    t.vvp = pick("VVP_EXE", "", "vvp");
    return t;
}

std::vector<std::pair<std::string, std::string>> RtlFlowTools::flowTools() const {
    return {{"OpenROAD-flow-scripts", flowDir}, {"gmake", make}, {"yosys", yosys}, {"openroad", openroad}, {"klayout", klayout}};
}

std::string RtlFlowTools::missing() const {
    std::string m;
    for (const auto& [name, path] : flowTools())
        if (path.empty()) m += (m.empty() ? "" : ", ") + name;
    return m;
}

std::string RtlFlowTools::simMissing(const std::string& platform) const {
    std::string m;
    for (const auto& [name, path] : {std::pair{"iverilog", iverilog}, {"vvp", vvp}, {"openroad", openroad}})
        if (path.empty()) m += (m.empty() ? "" : ", ") + std::string(name);
    if (const flow::Platform* p = flow::findPlatform(platform))
        for (const auto& f : p->cellVerilog)
            if (const std::string path = flow::platformFile(*p, flowDir, f); !fs::exists(path)) m += (m.empty() ? "" : ", ") + path;
    return m;
}

std::string rtlFlowConfigError(const RtlFlowConfig& cfg, const RtlFlowTools& tools) {
    static const std::regex identifier("[A-Za-z_][A-Za-z0-9_]*"), fileName("[A-Za-z0-9_.+-]+");
    const flow::Platform* p = flow::findPlatform(cfg.platform);
    if (!p) return "RTL flow: unknown platform '" + cfg.platform + "' (known: " + flow::knownPlatforms() + ")";
    if (const std::string m = tools.missing(); !m.empty()) return "RTL flow: missing " + m;
    if (const auto m = flow::missingFiles(*p, tools.flowDir); !m.empty()) return "RTL flow: platform " + p->name + " has no " + m.front();
    if (cfg.workDir.empty() || cfg.workDir.find_first_of(" \t") != std::string::npos)
        return "RTL flow: the work directory must be set and contain no spaces (make splits paths on them): '" + cfg.workDir + "'";
    if (cfg.top.empty() || cfg.verilog.empty()) return "RTL flow: a top module and Verilog sources are required";
    if (!std::regex_match(cfg.design, std::regex("[A-Za-z0-9_]+")))
        return "RTL flow: the design name may use only letters, digits and _";
    if (!std::regex_match(cfg.top, identifier)) return "RTL flow: the top module is not a plain identifier: '" + cfg.top + "'";
    if (!std::regex_match(cfg.clockPort, identifier)) return "RTL flow: the clock port is not a plain identifier: '" + cfg.clockPort + "'";
    if (!(cfg.clockPeriodNs > 0.0) || !(cfg.coreUtilization > 0.0 && cfg.coreUtilization < 100.0))
        return "RTL flow: the clock period must be > 0 and the utilization within (0, 100) %";
    std::set<std::string> names;
    for (const auto& v : cfg.verilog) {
        const fs::path path(v);
        const std::string name = path.filename().string();
        if (!path.is_absolute() || !fs::is_regular_file(path)) return "RTL flow: no source file at the absolute path '" + v + "'";
        if (!std::regex_match(name, fileName)) return "RTL flow: a source's file name may use only letters, digits and _ . + -: '" + name + "'";
        if (!names.insert(name).second) return "RTL flow: two sources are named " + name;
    }
    return "";
}

DRCResult readDrcReport(const std::string& lyrdb) {
    DRCResult r;
    if (!fs::exists(lyrdb)) {
        r.errors.push_back("no DRC report: " + lyrdb);
        return r;
    }
    const std::string text = slurp(lyrdb);
    size_t at = text.find("<items>");
    if (at == std::string::npos || text.find("</report-database>") == std::string::npos) {
        r.errors.push_back("incomplete DRC report: " + lyrdb);
        return r;
    }
    std::map<std::string, int> rules;  // a report's item names its rule, quoted when the name needs it
    static const std::string open = "<category>", close = "</category>";
    while ((at = text.find(open, at)) != std::string::npos) {
        const size_t end = text.find(close, at);
        if (end == std::string::npos) {
            r.errors.push_back("incomplete DRC report: " + lyrdb);
            return r;
        }
        std::string rule = text.substr(at + open.size(), end - at - open.size());
        rule.erase(std::remove(rule.begin(), rule.end(), '\''), rule.end());
        if (rule.find_first_not_of(' ') == std::string::npos) {  // asap7's deck names M1.S.2 "  ": its description has the name
            const size_t def = text.find("<name>" + rule + "</name>"), desc = text.find("<description>", def);
            rule = def == std::string::npos || desc == std::string::npos ? "(unnamed rule)" : text.substr(desc + 13, text.find_first_of(" <", desc + 13) - desc - 13);
        }
        ++rules[rule];
        at = end;
    }
    for (const auto& [rule, n] : rules) {
        DRCViolation v;
        v.ruleName = rule;
        v.count = n;
        v.severity = "error";
        r.violations.push_back(v);
        r.totalViolations += n;
    }
    r.errorCount = r.totalViolations;
    r.success = true;
    r.cleanDRC = r.totalViolations == 0;
    return r;
}

LVSResult readLvsReport(const std::string& lvsdb) {
    LVSResult r;
    if (!fs::exists(lvsdb)) {
        r.errors.push_back("no LVS report: " + lvsdb);
        return r;
    }
    // The cross-reference is the last section, "Z(" at the margin: each circuit X(layout schematic status), in it its
    // nets N, pins P, devices D and subcircuits X(layout schematic status), "()" for no counterpart, 1 (or W) a match
    std::ifstream in(lvsdb);
    static const std::regex entry(R"(\s*([NPDX])\((\S+) (\S+?)(?: (\S+?))?\)?)");
    bool xref = false, any = false;
    std::smatch m;
    for (std::string line; std::getline(in, line);) {
        if (line == "Z(") xref = true;
        if (!xref || !std::regex_match(line, m, entry)) continue;
        any = true;
        const char kind = m[1].str()[0];
        const bool layout = m[2] != "()", schematic = m[3] != "()";
        if (m[4] != "1" && m[4] != "W") ++r.mismatchCount;
        if (kind == 'D') r.deviceCountLayout += layout, r.deviceCountSchematic += schematic;
        if (kind == 'N') r.netCountLayout += layout, r.netCountSchematic += schematic;
    }
    if (!any) {
        r.errors.push_back("LVS report has no cross-reference (the netlists were not compared): " + lvsdb);
        return r;
    }
    r.success = true;
    r.matched = r.mismatchCount == 0;
    if (!r.matched) r.errors.push_back("layout and schematic netlists do not match (" + lvsdb + ")");
    return r;
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
    if (const std::string e = rtlFlowConfigError(cfg, tools); !e.empty()) return fail(e);
    const flow::Platform& pdk = *flow::findPlatform(cfg.platform);

    // Sources are copied in, so their own paths may hold spaces
    const fs::path work = fs::absolute(cfg.workDir), src = work / "src" / cfg.design;
    std::error_code ec;
    fs::create_directories(src, ec);
    std::string verilog;
    for (const auto& v : cfg.verilog) {
        const std::string name = fs::path(v).filename().string();
        fs::copy_file(v, src / name, fs::copy_options::overwrite_existing, ec);
        if (ec) return fail("RTL flow: cannot copy " + v + ": " + ec.message());
        verilog += " " + (src / name).string();
    }
    const fs::path sdc = src / "constraint.sdc", mk = src / "config.mk";
    const double period = cfg.clockPeriodNs / pdk.nsPerTimeUnit;  // the Liberty's time unit
    std::ofstream sdcFile(sdc);
    sdcFile.precision(12);
    sdcFile << "create_clock -name core_clock -period " << period << " [get_ports {" << cfg.clockPort
            << "}]\n"
               "set non_clock_inputs [delete_from_list [all_inputs] [get_ports {" << cfg.clockPort << "}]]\n"
               "set_input_delay " << 0.2 * period << " -clock core_clock $non_clock_inputs\n"
               "set_output_delay " << 0.2 * period << " -clock core_clock [all_outputs]\n";
    sdcFile.close();
    std::ofstream mkFile(mk);
    mkFile << "export DESIGN_NICKNAME = " << cfg.design << "\nexport DESIGN_NAME = " << cfg.top
           << "\nexport PLATFORM = " << cfg.platform << "\nexport VERILOG_FILES =" << verilog
           << "\nexport SDC_FILE = " << sdc.string() << "\nexport CORE_UTILIZATION = " << cfg.coreUtilization
           << "\nexport PLACE_DENSITY_LB_ADDON = 0.2\nexport TNS_END_PERCENT = 100\n";
    for (const auto& line : pdk.orfsConfig) mkFile << "export " << line << "\n";
    mkFile.close();

    const std::string threads = std::to_string(std::max(1, (int)std::thread::hardware_concurrency() - 2));
    // Runs one tool, streaming whole lines to the log (and ORFS's stages to `stage`)
    auto runTool = [&](const std::string& exe, const std::vector<std::string>& args, std::string* output = nullptr) {
        return run(tools, work, exe, args, cancel, res.error, output, [&](const std::string& line) {
            if (log) log(line, false);
            static const std::regex stageRe(R"(flow\.sh ([1-6])_(\S+))");
            std::smatch m;
            if (stage && std::regex_search(line, m, stageRe)) {
                static const double at[] = {0, 0.05, 0.15, 0.25, 0.40, 0.50, 0.80};
                stage(m[1].str() + "_" + m[2].str(), at[m[1].str()[0] - '0']);
            }
        });
    };

    // Synthesis -> floorplan -> PDN -> placement -> CTS -> routing -> fill -> GDS (ORFS "finish")
    if (stage) stage("synthesis", 0.0);
    if (!runTool(tools.make, {"-C", tools.flowDir, "DESIGN_CONFIG=" + mk.string(), "WORK_HOME=" + work.string(),
                              "YOSYS_EXE=" + tools.yosys, "OPENROAD_EXE=" + tools.openroad, "KLAYOUT_CMD=" + tools.klayout,
                              "NUM_CORES=" + threads, "finish"}))
        return fail("RTL flow: " + res.error);

    const fs::path tail = fs::path(cfg.platform) / cfg.design / "base";
    const fs::path results = work / "results" / tail, reports = work / "reports" / tail, logs = work / "logs" / tail;
    auto file = [&](const std::string& f) { return flow::platformFile(pdk, tools.flowDir, f); };
    res.gds = (results / "6_final.gds").string();
    res.reportDir = reports.string();

    const std::string stat = slurp(reports / "synth_stat.txt");
    // Hierarchical designs list every module and the top last ("for top module"), flat ones one
    res.synth.estimatedArea = num(lastMatch(stat, std::regex(R"(Chip area for (?:top )?module '[^']*': ([0-9.eE+-]+))")));
    res.synth.cellCount = (int)num(lastMatch(stat, std::regex(R"(\n\s*(\d+)\s+\S+\s+\d+\s+\S+\s+cells\n)")));
    res.synth.success = res.synth.cellCount > 0;

    // Sign-off reads its reports: a missing one is an error, never a clean zero
    const std::string finish = slurp(reports / "6_finish.rpt"), report = slurp(logs / "6_report.log");
    auto& pnr = res.pnr;
    const std::string wns = lastMatch(finish, std::regex(R"(\nwns max ([0-9.eE+-]+))"));
    if (wns.empty()) return fail("RTL flow: " + (reports / "6_finish.rpt").string() + " reports no wns (timing unknown)");
    pnr.timing.wns = num(wns) * pdk.nsPerTimeUnit;  // ns, on every platform
    pnr.timing.tns = num(lastMatch(finish, std::regex(R"(\ntns max ([0-9.eE+-]+))"))) * pdk.nsPerTimeUnit;
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
    if (!fs::exists(reports / "5_route_drc.rpt")) return fail("RTL flow: no 5_route_drc.rpt (routing DRC unknown)");
    const std::string routeDrc = slurp(reports / "5_route_drc.rpt");
    for (size_t at = 0; (at = routeDrc.find("violation type", at)) != std::string::npos; ++at) ++pnr.routing.drcViolations;
    // check_antennas logs its count ("[INFO ANT-0002] Found N net violations.") to the route log; its report file
    // (drt_antennas.log) lists violations only and is empty when there are none
    const std::string antenna = lastMatch(slurp(logs / "5_2_route.log"), std::regex(R"(Found (\d+) net violations)"));
    if (antenna.empty()) return fail("RTL flow: " + (logs / "5_2_route.log").string() + " reports no antenna check");
    pnr.routing.antennaViolations = (int)num(antenna);
    pnr.routing.drcClean = pnr.routing.drcViolations == 0 && pnr.routing.antennaViolations == 0;
    pnr.success = fs::exists(res.gds);
    if (!pnr.success) return fail("RTL flow: no GDS was written");

    // DRC: the platform's deck, read back from the report it writes
    if (stage) stage("DRC", 0.88);
    std::string drcLog;
    const fs::path drcReport = reports / "6_drc.lyrdb";
    std::vector<std::string> drcArgs{"-b", "-rd", "in_gds=" + res.gds, "-rd", "report_file=" + drcReport.string(), "-rd", "threads=" + threads};
    for (const auto& d : pdk.drcDefines) {
        drcArgs.push_back("-rd");
        drcArgs.push_back(d);
    }
    drcArgs.push_back("-r");
    drcArgs.push_back(file(pdk.drcDeck));
    if (!runTool(tools.klayout, drcArgs, &drcLog)) return fail("RTL flow: DRC " + res.error);
    std::ofstream(logs / "6_drc.log") << drcLog;
    res.drc = readDrcReport(drcReport.string());
    if (!res.drc.success) return fail("RTL flow: DRC: " + res.drc.errors.front());

    // LVS needs the platform's deck: without one nothing was compared, and the run is not clean
    if (pdk.lvsDeck.empty()) {
        res.lvs.errors.push_back(pdk.noLvs);
        res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (stage) stage("done", 1.0);
        return res;
    }

    // LVS: the netlist must include the decap and fill cells (physical-only, but decaps hold
    // transistors between VDD and VSS), then the platform's KLayout LVS deck
    if (stage) stage("LVS", 0.95);
    const fs::path cdl = results / "6_lvs.cdl", cdlTcl = results / "6_lvs_cdl.tcl";
    std::ofstream(cdlTcl) << "read_db " << (results / "6_final.odb").string() << "\nwrite_cdl -include_fillers -masters "
                          << file(pdk.cdl) << " " << cdl.string() << "\n";
    if (!runTool(tools.openroad, {"-no_init", "-exit", cdlTcl.string()})) return fail("RTL flow: CDL netlist " + res.error);
    std::ofstream(cdl, std::ios::app) << slurp(file(pdk.cdl));
    // IHP's deck extracts an antenna cell's diodes in the parent (the cell's wells merge with
    // its row's), so the layout cell comes out empty, simplify purges it after align, and the
    // schematic's antenna instances are left without a counterpart. A copy of the deck
    // flattens the antenna cells on both sides before align: their diodes are still compared,
    // one level up.
    // ponytail: the -rd names and this patching are the shape of IHP's deck, the only one; M3's asap7 deck shows what generalizes
    const fs::path lvsDeck = file(pdk.lvsDeck), lvsDir = lvsDeck.parent_path(), deck = results / "6_lvs.lvs";
    std::string deckText = slurp(lvsDeck);
    const std::string inc = "# %include rule_decks/", align = "\nalign\n";
    if (deckText.find(inc) == std::string::npos || deckText.find(align) == std::string::npos)
        return fail("RTL flow: the LVS deck changed (no rule_decks includes or align): update rtl_flow.cpp");
    for (size_t at = 0; (at = deckText.find(inc, at)) != std::string::npos; at += inc.size())
        deckText.replace(at, inc.size(), "# %include " + (lvsDir / "rule_decks").string() + "/");
    deckText.replace(deckText.find(align), align.size(),
                     "\nnetlist.flatten_circuit(\"" + pdk.lvsFlatten + "\")\nschematic.flatten_circuit(\"" + pdk.lvsFlatten + "\")\nalign\n");
    std::ofstream(deck) << deckText;
    std::string lvsLog;
    if (!runTool(tools.klayout, {"-b", "-rd", "input=" + res.gds, "-rd", "topcell=" + cfg.top, "-rd", "schematic=" + cdl.string(),
                                 "-rd", "report=" + (results / "6_lvs.lvsdb").string(), "-rd",
                                 "target_netlist=" + (results / "6_lvs_extracted.cir").string(), "-rd", "run_mode=deep", "-r",
                                 deck.string()},
                 &lvsLog))
        return fail("RTL flow: LVS " + res.error);
    std::ofstream(logs / "6_lvs.log") << lvsLog;
    res.lvs = readLvsReport((results / "6_lvs.lvsdb").string());
    if (!res.lvs.success) return fail("RTL flow: LVS: " + res.lvs.errors.front());

    res.success = pnr.timing.timingClean && pnr.routing.drcClean && res.drc.cleanDRC && res.lvs.matched;
    res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (stage) stage("done", 1.0);
    return res;
}

double staMinPeriodNs(const std::string& results, const std::string& platform, const RtlFlowTools& tools,
                      double ioDelayNs, std::string& error) {
    // At a period P the worst slack S says the paths need P - S: measure at a loose 100 ns
    const flow::Platform* pdk = flow::findPlatform(platform);
    if (!pdk) {
        error = "unknown platform '" + platform + "' (known: " + flow::knownPlatforms() + ")";
        return -1.0;
    }
    const double unit = pdk->nsPerTimeUnit;  // the Liberty's time unit, in ns
    const fs::path res = results, tcl = res / "sta_min_period.tcl";
    std::ofstream sta(tcl);
    for (const auto& lib : pdk->liberty) sta << "read_liberty " << flow::platformFile(*pdk, tools.flowDir, lib) << "\n";
    sta << "read_db " << (res / "6_final.odb").string() << "\ncreate_clock -name clk -period " << 100.0 / unit << " [get_ports clk]\n"
                       << "set_propagated_clock [all_clocks]\n"
                       << "set_input_delay " << ioDelayNs / unit << " -clock clk [delete_from_list [all_inputs] [get_ports clk]]\n"
                       << "set_output_delay " << ioDelayNs / unit << " -clock clk [all_outputs]\nsource "
                       << flow::platformFile(*pdk, tools.flowDir, pdk->setRC)
                       << "\nestimate_parasitics -global_routing\nputs \"SLACK [sta::worst_slack -max]\"\nexit\n";
    sta.close();
    std::string out;
    if (!run(tools, res, tools.openroad, {"-no_init", "-exit", tcl.string()}, nullptr, error, &out, nullptr)) return -1.0;
    std::smatch m;
    if (!std::regex_search(out, m, std::regex(R"(SLACK (-?[0-9.eE+-]+))"))) {
        error = "STA printed no slack";
        return -1.0;
    }
    return 100.0 - std::stod(m[1]) * unit;
}

// Everything Icarus needs for the routed design: the cells with their specify blocks made plain, the netlist with plain names,
// and design.sdf from OpenROAD with the routed design's parasitics, all in work. False with error.
static bool prepareGateSources(const flow::Platform& platform, const RtlFlowTools& tools, const fs::path& results, const fs::path& work,
                               const std::atomic<bool>* cancel, std::string& error, std::vector<std::string>& sources, const FlowLog& log) {
    const flow::Platform* pdk = &platform;
    // SDF from the layout: OpenROAD with the routed design's global-route parasitics
    auto file = [&](const std::string& f) { return flow::platformFile(*pdk, tools.flowDir, f); };
    std::ofstream sdfTcl(work / "sdf.tcl");
    for (const auto& lib : pdk->liberty) sdfTcl << "read_liberty " << file(lib) << "\n";
    sdfTcl << "read_db " << (results / "6_final.odb").string() << "\nread_sdc "
                                    << (results / "6_final.sdc").string() << "\nsource " << file(pdk->setRC)
                                    << "\nestimate_parasitics -global_routing\nwrite_sdf " << (work / "raw.sdf").string()
                                    << "\nexit\n";
    sdfTcl.close();
    if (!run(tools, work, tools.openroad, {"-no_init", "-exit", (work / "sdf.tcl").string()}, cancel, error, nullptr, [&](const std::string& line) { if (log) log(line, false); })) {
        error = "SDF " + error;
        return false;
    }
    // Icarus splits escaped names on '.', so the netlist and the SDF get the same plain names;
    // it ignores "min::max" delays, so they become min:max:max
    const std::string sdf = slurp(work / "raw.sdf");
    std::ofstream(work / "netlist.v") << plainNames(slurp(results / "6_final.v"));
    std::ofstream(work / "design.sdf") << std::regex_replace(std::regex_replace(sdf, std::regex(R"(\\(.))"), "_"),
                                                             std::regex(R"(\((-?[\d.]+)::(-?[\d.]+)\))"), "($1:$2:$2)");
    // IHP's flops clock on delayed_* copies of their pins, which only the timing checks drive
    // and Icarus does not run them: alias them. The specify blocks become plain pin-to-pin
    // paths, the shape of the SDF's IOPATHs (2.8x faster to annotate and run).
    // ponytail: state-dependent (COND) delays collapse to the path's maximum.
    const std::string cells = aliasDelayed(slurp(file(pdk->cellVerilog.front())));
    std::string patched;
    static const std::regex specRe(R"(specify([\s\S]*?)endspecify)"),
        pathRe(R"(\(\s*(?:posedge|negedge)?\s*(\w+)\s*=>\s*\(\s*(\w+)\s*:)");
    auto it = cells.cbegin();
    for (std::sregex_iterator m(cells.begin(), cells.end(), specRe), end; m != end; ++m) {
        patched.append(it, (*m)[0].first);
        std::set<std::pair<std::string, std::string>> paths;
        std::string block = "specify\n";
        const std::string body = (*m)[1];
        for (std::sregex_iterator p(body.begin(), body.end(), pathRe); p != end; ++p)
            if (paths.insert({(*p)[1], (*p)[2]}).second) block += "\t(" + (*p)[1].str() + " => " + (*p)[2].str() + ") = (0.0, 0.0);\n";
        patched += block + "endspecify";
        it = (*m)[0].second;
    }
    patched.append(it, cells.cend());
    std::ofstream(work / "cells.v") << patched;
    sources = {(work / "cells.v").string()};
    for (size_t i = 1; i < pdk->cellVerilog.size(); ++i) sources.push_back(file(pdk->cellVerilog[i]));
    sources.push_back((work / "netlist.v").string());
    return true;
}

FirmwareSimResult runFirmware(const FirmwareSimConfig& cfg, const RtlFlowTools& tools, FlowLog log,
                              const std::atomic<bool>* cancel) {
    const auto t0 = std::chrono::steady_clock::now();
    FirmwareSimResult res;
    auto fail = [&](const std::string& e) {
        res.error = e;
        res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (log) log(e, true);
        return res;
    };
    const bool gate = cfg.rtl.empty();
    const flow::Platform* pdk = flow::findPlatform(cfg.platform);
    if (!pdk) return fail("firmware sim: unknown platform '" + cfg.platform + "' (known: " + flow::knownPlatforms() + ")");
    if (gate && !pdk->gateSim) return fail("firmware sim: gate-level simulation is not checked on " + pdk->name);
    if (const std::string m = tools.simMissing(cfg.platform); !m.empty()) return fail("firmware sim: missing " + m);
    if (cfg.workDir.empty() || cfg.rom.empty()) return fail("firmware sim: a work directory and firmware are required");
    if (!(cfg.clockPeriodNs > 0.0)) return fail("firmware sim: the clock period must be > 0");
    const fs::path work = fs::absolute(cfg.workDir), results = cfg.results;
    std::error_code ec;
    fs::create_directories(work, ec);
    if (gate && !fs::exists(results / "6_final.v")) return fail("firmware sim: no routed netlist in " + cfg.results);
    auto tool = [&](const std::string& exe, const std::vector<std::string>& args, std::string* out = nullptr) {
        return run(tools, work, exe, args, cancel, res.error, out, [&](const std::string& line) { if (log) log(line, false); });
    };

    {
        std::ofstream rom(work / "rom.hex");
        char word[16];
        for (uint32_t w : cfg.rom) {
            std::snprintf(word, sizeof word, "%08x\n", w);
            rom << word;
        }
    }
    std::vector<std::string> sources;
    if (gate) {
        std::string why;
        if (!prepareGateSources(*pdk, tools, results, work, cancel, why, sources, log)) return fail("firmware sim: " + why);
    } else {
        sources = cfg.rtl;
    }

    // The testbench: soc/'s memory map around the core; the memories answer combinationally
    // after memReadDelayNs (a single-cycle core reads within the cycle), stores land at the edge
    const double half = cfg.clockPeriodNs / 2, mem = std::max(0.0, cfg.memReadDelayNs);
    std::ofstream(work / "tb.v")
        << "`timescale 1ns/1ps\nmodule tb;\n"
           "  reg clk = 0, reset = 1;\n  wire [31:0] pc, aluout, writedata, instr, readdata;\n  wire memwrite, memread, suspend;\n"
           "  reg [31:0] rom [0:1023];\n  reg [31:0] sram [0:1023];\n"
           "  initial $readmemh(\"rom.hex\", rom);\n"
           "  assign #" << mem << " instr = rom[pc[11:2]];\n"
           "  assign #" << mem << " readdata = aluout[31:28] == 4'h1 ? sram[aluout[11:2]] : rom[aluout[11:2]];\n"
           "  " << cfg.top << " dut(.clk(clk), .reset(reset), .instr(instr), .readdata(readdata), .pc(pc), .aluout(aluout),\n"
           "      .writedata(writedata), .memwrite(memwrite), .memread(memread), .suspend(suspend));\n"
           "  always #" << half << " clk = ~clk;\n"
           "  integer cycles = 0;\n"
           "  always @(posedge clk) if (!reset) begin\n"
           "    if (memwrite && aluout[31:28] == 4'h2) $display(\"UART %0d\", writedata[7:0]);\n"
           "    if (memwrite && aluout[31:28] == 4'h1) sram[aluout[11:2]] <= writedata;\n"
           "    if (suspend === 1'b1) begin $display(\"HALT %0d\", cycles + 1); $finish; end\n"  // the ECALL's own cycle
           "    cycles = cycles + 1;\n"
           "    if (cycles > " << cfg.maxCycles << ") begin $display(\"TIMEOUT\"); $finish; end\n"
           "  end\n"
           "  initial begin\n"
        << (gate ? "    $sdf_annotate(\"design.sdf\", dut, , \"sdf.log\", \"MAXIMUM\");\n" : "")
        << "    #" << 2.25 * cfg.clockPeriodNs << " reset = 0;\n  end\nendmodule\n";
    std::vector<std::string> args{"-g2012", "-o", (work / "sim.vvp").string(), (work / "tb.v").string()};
    if (gate) args.insert(args.begin(), "-gspecify");
    args.insert(args.end(), sources.begin(), sources.end());
    if (!tool(tools.iverilog, args)) return fail("firmware sim: iverilog " + res.error);
    std::string out;
    if (!tool(tools.vvp, {"-n", (work / "sim.vvp").string()}, &out)) return fail("firmware sim: vvp " + res.error);
    std::stringstream lines(out);
    for (std::string line; std::getline(lines, line);) {
        if (line.rfind("UART ", 0) == 0) res.uart += (char)std::atoi(line.c_str() + 5);
        else if (line.rfind("HALT ", 0) == 0) { res.cycles = std::atol(line.c_str() + 5); res.ok = true; }
        else if (line == "TIMEOUT") res.timedOut = true;
    }
    res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (!res.ok) res.error = res.timedOut ? "no ECALL within " + std::to_string(cfg.maxCycles) + " cycles" : "simulation ended without ECALL";
    return res;
}

TestbenchResult runTestbench(const TestbenchConfig& cfg, const RtlFlowTools& tools, FlowLog log, const std::atomic<bool>* cancel) {
    const auto t0 = std::chrono::steady_clock::now();
    TestbenchResult res;
    const auto finish = [&](const std::string& e) {
        res.error = e;
        res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (log && !e.empty()) log(e, true);
        return res;
    };
    static const std::regex name(R"(^[A-Za-z_][A-Za-z0-9_$]*$)"), path(R"(^[A-Za-z_][A-Za-z0-9_$.]*$)");
    if (!std::regex_match(cfg.top, name)) return finish("testbench: its top module must be a plain Verilog name");
    if (cfg.testbench.empty()) return finish("testbench: no testbench files");
    if (!(cfg.timeLimitNs > 0)) return finish("testbench: the time limit must be > 0");
    if (cfg.passPattern.empty()) return finish("testbench: a pass pattern is required, so that a testbench that says nothing does not pass");
    if (tools.iverilog.empty() || tools.vvp.empty()) return finish("testbench: missing Icarus Verilog (iverilog, vvp)");
    for (const auto& d : cfg.defines)
        if (d.find_first_of(" \n\t;") != std::string::npos) return finish("testbench: define '" + d + "' has a space or separator");
    const fs::path work = fs::absolute(cfg.workDir);
    std::error_code ec;
    fs::create_directories(work, ec);
    const auto lines = [&](const std::string& line) { if (log) log(line, false); };

    std::vector<std::string> sources;
    const bool gate = cfg.rtl.empty();
    if (gate) {
        const flow::Platform* pdk = flow::findPlatform(cfg.platform);
        if (!pdk) return finish("testbench: unknown platform '" + cfg.platform + "' (known: " + flow::knownPlatforms() + ")");
        if (!pdk->gateSim) return finish("testbench: gate-level simulation is not checked on " + pdk->name);
        if (const std::string m = tools.simMissing(cfg.platform); !m.empty()) return finish("testbench: missing " + m);
        if (!std::regex_match(cfg.dut, path)) return finish("testbench: gate level needs the design's instance path, as tb.dut");
        if (!fs::exists(fs::path(cfg.results) / "6_final.v")) return finish("testbench: no routed netlist in " + cfg.results);
        std::string why;
        if (!prepareGateSources(*pdk, tools, cfg.results, work, cancel, why, sources, log)) return finish("testbench: " + why);
    } else {
        sources = cfg.rtl;
    }
    // A second top module beside the testbench: it records every signal, annotates the layout's delays and ends a run that
    // never finishes on its own
    std::ofstream(work / "harness.v") << "`timescale 1ns/1ps\nmodule openvlsi_harness;\n  initial begin\n"
                                         "    $dumpfile(\"dump.vcd\");\n    $dumpvars(0, " << cfg.top << ");\n"
                                      << (gate ? "    $sdf_annotate(\"design.sdf\", " + cfg.dut + ", , \"sdf.log\", \"MAXIMUM\");\n" : std::string())
                                      << "    #" << cfg.timeLimitNs << " $display(\"OPENVLSI_TIME_LIMIT\");\n    $finish;\n  end\nendmodule\n";
    std::vector<std::string> args{"-g2012", "-s", cfg.top, "-s", "openvlsi_harness", "-o", (work / "sim.vvp").string()};
    if (gate) args.insert(args.begin(), "-gspecify");
    for (const auto& d : cfg.defines) args.push_back("-D" + d);
    args.push_back((work / "harness.v").string());
    args.insert(args.end(), cfg.testbench.begin(), cfg.testbench.end());
    args.insert(args.end(), sources.begin(), sources.end());
    std::string why;
    if (!run(tools, work, tools.iverilog, args, cancel, why, nullptr, lines)) return finish("the testbench or design does not compile: " + why);
    res.ran = true;
    std::string out;
    if (!run(tools, work, tools.vvp, {"-n", (work / "sim.vvp").string()}, cancel, why, &out, lines)) return finish("vvp: " + why);
    res.output = out;
    res.vcd = (work / "dump.vcd").string();
    if (out.find("OPENVLSI_TIME_LIMIT") != std::string::npos) return finish("it reached the time limit of " + std::to_string(cfg.timeLimitNs) + " ns without finishing");
    static const std::regex bad(R"((^|\n)\s*(FATAL|FAIL|ERROR)\b)");
    if (std::regex_search(out, bad)) return finish("the testbench reported a failure");
    if (out.find(cfg.passPattern) == std::string::npos) return finish("it finished without printing '" + cfg.passPattern + "'");
    res.passed = true;
    return finish("");
}

}  // namespace acutesim::compute::orchestration
