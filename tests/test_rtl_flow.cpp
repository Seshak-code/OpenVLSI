// test_rtl_flow — Roadmap Phase 6: RTL -> GDS on IHP SG13G2 through the app's flow driver.
// Takes ORFS's gcd design (about 300 cells) through synthesis, place and route, fill, GDS,
// IHP's maximal DRC deck and LVS, and checks the reports it reads back. Needs the tools of
// RtlFlowTools::detect() (Roadmap Phase 6); without them it says what is missing and passes.
// RTL_FLOW_DESIGN=<dir with .v> RTL_FLOW_TOP=<top> RTL_FLOW_PERIOD=<ns> runs another design.
#include "flow/rtl_flow.h"

#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <string>

using namespace acutesim::compute::orchestration;
namespace fs = std::filesystem;

static int g_fail = 0;
static void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++g_fail;
}

int main() {
    const RtlFlowTools tools = RtlFlowTools::detect();
    if (!tools.missing().empty()) {
        std::printf("RTL flow tools not found (%s): skipped\n", tools.missing().c_str());
        return 0;
    }
    RtlFlowConfig cfg;
    const char* dir = std::getenv("RTL_FLOW_DESIGN");
    const fs::path src = dir ? fs::path(dir) : fs::path(tools.flowDir) / "designs" / "src" / "gcd";
    for (const auto& e : fs::directory_iterator(src))
        if (e.path().extension() == ".v") cfg.verilog.push_back(e.path().string());
    cfg.top = std::getenv("RTL_FLOW_TOP") ? std::getenv("RTL_FLOW_TOP") : "gcd";
    cfg.design = "test_" + cfg.top;
    cfg.clockPeriodNs = std::getenv("RTL_FLOW_PERIOD") ? std::atof(std::getenv("RTL_FLOW_PERIOD")) : 10.0;
    cfg.workDir = (fs::temp_directory_path() / "openvlsi_rtl_flow").string();
    double lastProgress = -1.0;
    bool monotone = true;
    std::deque<std::string> tail;  // the last log lines, printed if the flow fails
    auto log = [&](const std::string& line, bool) {
        tail.push_back(line);
        if (tail.size() > 30) tail.pop_front();
    };
    const RtlFlowResult r = runRtlToGds(cfg, tools, log, [&](const std::string& stage, double p) {
        monotone = monotone && p >= lastProgress;
        lastProgress = p;
        std::printf("  .. %-24s %3.0f %%\n", stage.c_str(), 100 * p);
    });
    std::printf("== %s on IHP SG13G2: %.0f s%s%s\n", cfg.top.c_str(), r.seconds, r.error.empty() ? "" : ", ",
                r.error.c_str());
    if (!r.error.empty())
        for (const auto& line : tail) std::printf("  | %s\n", line.c_str());
    check(r.error.empty() && fs::exists(r.gds), "flow ran to a GDS (" + r.gds + ")");
    check(r.synth.cellCount > 0 && r.synth.estimatedArea > 0,
          "synthesis: " + std::to_string(r.synth.cellCount) + " cells, " + std::to_string(r.synth.estimatedArea) + " um^2");
    check(r.pnr.placement.totalArea > 0 && r.pnr.placement.utilization > 0,
          "placement: " + std::to_string(r.pnr.placement.totalArea) + " um^2 at " +
              std::to_string(100 * r.pnr.placement.utilization) + " % utilization");
    check(r.pnr.timing.timingClean, "timing met at " + std::to_string(cfg.clockPeriodNs) + " ns (WNS " +
                                        std::to_string(r.pnr.timing.wns) + " ns)");
    check(r.pnr.power.totalPower > 0, "power " + std::to_string(r.pnr.power.totalPower) + " mW");
    check(r.pnr.routing.drcClean, "routing: " + std::to_string(r.pnr.routing.drcViolations) + " DRC, " +
                                      std::to_string(r.pnr.routing.antennaViolations) + " antenna violations");
    check(r.drc.success && r.drc.cleanDRC, "IHP maximal DRC deck: " + std::to_string(r.drc.totalViolations) + " violations");
    check(r.lvs.success && r.lvs.matched, "LVS: layout and schematic netlists match");
    check(r.success && monotone && lastProgress == 1.0, "flow reports success, progress ran 0 -> 100 %");
    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
