// test_firmware_on_silicon — Roadmap Phase 7 gate: the hello-world firmware on the RV32I core
// itself, from its RTL and from its routed IHP SG13G2 netlist with layout timing (SDF), against
// the instruction-set model of soc/. Timing must agree with STA: the SDF run is correct at
// the period the layout was closed for, and fails once the clock is faster than its paths.
// Needs the RTL flow tools and Icarus (RtlFlowTools); without them it says what is missing and
// passes. The routed core comes from test_rtl_flow's RV32I run (7 ns), or is made here.
#include "flow/rtl_flow.h"
#include "soc/soc.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
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
    if (const std::string m = tools.missing() + tools.simMissing(); !m.empty()) {
        std::printf("tools not found (%s): skipped\n", m.c_str());
        return 0;
    }
    const fs::path src = fs::path(tools.flowDir) / "designs" / "src" / "riscv32i";
    std::vector<std::string> rtl;
    for (const auto& e : fs::directory_iterator(src))  // the core: not top.v's ROM/dmem wrappers
        if (e.path().extension() == ".v" && e.path().stem() != "top" && e.path().stem() != "rom" && e.path().stem() != "dmem")
            rtl.push_back(e.path().string());

    RtlFlowConfig flow;
    flow.design = "test_riscv";
    flow.top = "riscv";
    flow.verilog = rtl;
    flow.clockPeriodNs = 7.0;
    flow.workDir = (fs::temp_directory_path() / "openvlsi_rtl_flow").string();
    const fs::path results = fs::path(flow.workDir) / "results" / flow.platform / flow.design / "base";
    if (!fs::exists(results / "6_final.odb")) {
        std::printf("== routing the RV32I core (7 ns) first\n");
        const RtlFlowResult r = runRtlToGds(flow, tools);
        check(r.success, "RV32I placed, routed, DRC and LVS clean" + (r.error.empty() ? "" : ": " + r.error));
        if (!r.success) return 1;
    }

    // The reference: the instruction-set model (single-cycle core: one instruction per cycle)
    const auto rom = soc::helloFirmware();
    const soc::SocRun iss = soc::runSoc(rom, soc::SramTiming{});
    std::printf("== ISS: %llu instructions, \"%s\"\n", (unsigned long long)iss.instructions, iss.uart.c_str());
    // SRAM read time from SPICE when the IHP models are there (Phase 5), else a typical 0.1 ns
    const std::string datasets = SOC_DATASETS;
    const soc::SramTiming sram = soc::characterizeSram(datasets, 1.0 / 7e-9);
    const double readNs = sram.error.empty() ? sram.readDelay * 1e9 : 0.1;
    std::printf("== memory read %.4f ns (%s)\n", readNs, sram.error.empty() ? "SPICE, IHP 6T cell" : "assumed");

    auto sim = [&](bool gate, double periodNs) {
        FirmwareSimConfig c;
        if (!gate) c.rtl = rtl;
        c.results = results.string();
        c.rom = rom;
        c.clockPeriodNs = periodNs;
        c.memReadDelayNs = readNs;
        c.maxCycles = 4 * (long)iss.instructions;
        c.workDir = (fs::temp_directory_path() / "openvlsi_fw_sim" / (gate ? "gate" : "rtl")).string();
        return runFirmware(c, tools);
    };
    auto good = [&](const FirmwareSimResult& r) { return r.ok && r.uart == iss.uart && r.cycles == (long)iss.instructions; };

    const FirmwareSimResult rtlRun = sim(false, 7.0);
    check(good(rtlRun), "RTL: \"" + rtlRun.uart + "\" in " + std::to_string(rtlRun.cycles) + " cycles (" +
                            std::to_string(rtlRun.seconds) + " s)" + (rtlRun.error.empty() ? "" : ": " + rtlRun.error));
    const FirmwareSimResult gateRun = sim(true, 7.0);
    check(good(gateRun), "routed netlist + SDF at 7 ns: \"" + gateRun.uart + "\" in " + std::to_string(gateRun.cycles) +
                             " cycles (" + std::to_string(gateRun.seconds) + " s)" + (gateRun.error.empty() ? "" : ": " + gateRun.error));
    const FirmwareSimResult fast = sim(true, 2.5);
    check(!good(fast), "at 2.5 ns the layout's paths are too slow: the run is wrong (\"" + fast.uart + "\")");

    // The fastest period the SDF run survives, against STA. STA bounds the worst path over every
    // instruction and input; this firmware exercises some (adds, branches, word loads and stores,
    // no shifts), so its limit sits a little under STA's floor (the core's own paths, no I/O
    // budget). The closure limit adds the 20 % per-side I/O budget the layout was signed off with.
    double lo = 2.5, hi = 7.0;
    while (hi - lo > 0.125) {
        const double mid = 0.5 * (lo + hi);
        (good(sim(true, mid)) ? hi : lo) = mid;
    }
    std::string err;
    const double floorNs = staMinPeriodNs(results.string(), flow.platform, tools, 0.0, err);
    const double closureNs = staMinPeriodNs(results.string(), flow.platform, tools, 0.2 * 7.0, err);
    std::printf("== fastest correct SDF run %.3f ns; STA: %.3f ns without I/O budget, %.3f ns with 1.4 ns per side%s\n", hi,
                floorNs, closureNs, err.empty() ? "" : (" (" + err + ")").c_str());
    check(err.empty() && hi <= floorNs + 0.125 && hi >= 0.85 * floorNs && closureNs <= 7.0 + 1e-3,
          "the SDF run's limit is within 15 % under STA's floor, and the signed-off 7 ns is STA-clean");

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
