#pragma once
// RTL -> GDS on a real PDK (Roadmap Phase 6): OpenROAD-flow-scripts (Yosys, OpenROAD) for
// synthesis through routing and fill, then the platform's own KLayout decks for DRC and LVS.
// The flow is ORFS's maintained, taped-out recipe; this file only writes the design config,
// runs it and reads its reports into the result structs the commands show. Qt-free.
#include "yosys_log_parser.h"        // SynthesisResult
#include "openroad_report_parser.h"  // PnRResult
#include "klayout_report_parser.h"   // DRCResult, LVSResult

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace flow {

// What the flow needs of one ORFS platform. Files are relative to its directory (platforms/<name>), or to $HOME with ~/.
struct Platform {
    std::string name;
    std::string timeUnit;     // its Liberty's ("1ns"): SDC periods and ORFS's slacks are in it
    double nsPerTimeUnit;
    std::string drcDeck;
    std::vector<std::string> drcDefines;  // -rd name=value the deck needs
    std::string lvsDeck;      // "": none, and noLvs says why (a run with no LVS is never clean)
    std::string noLvs;
    std::string cdl;          // the cells' netlists LVS compares against
    std::string lvsFlatten;   // cells flattened on both sides before LVS aligns the netlists
    std::vector<std::string> liberty;      // the corner ORFS runs by default
    std::string setRC;
    std::vector<std::string> orfsConfig;   // extra config.mk lines
    std::vector<std::string> cellVerilog;  // gate-level models, the patched file first (runFirmware)
    bool gateSim;             // runFirmware's patching of cellVerilog has been checked on this platform
    std::string layerProperties;  // KLayout .lyp: names and colours of its layers
    std::string tech;         // the layout process (layout/tech.h) a run's layout attaches; "" none
};

const std::vector<Platform>& platforms();
const Platform* findPlatform(const std::string& name);  // nullptr: unknown
std::string knownPlatforms();                           // their names, comma separated
std::string platformFile(const Platform& p, const std::string& flowDir, const std::string& file);
std::vector<std::string> missingFiles(const Platform& p, const std::string& flowDir);  // what a flow run needs and is not there

}  // namespace flow

namespace acutesim::compute::orchestration {

struct RtlFlowTools {
    std::string flowDir;   // ORFS flow/ (holds Makefile and platforms/)
    std::string make, yosys, openroad, klayout;
    std::string binDir;    // prepended to PATH (the klayout wrapper, a pmap shim for IHP's LVS deck)
    std::string iverilog, vvp;  // firmware simulation (Icarus: it annotates SDF, Verilator does not)

    // ORFS_FLOW_HOME / OPENROAD_EXE / KLAYOUT_CMD / YOSYS_EXE / MAKE_EXE override, and one naming nothing leaves the tool
    // missing; else ~/eda (where the source builds live: their build tools break on paths with spaces), else PATH
    static RtlFlowTools detect();
    std::vector<std::pair<std::string, std::string>> flowTools() const;  // a flow run's tools: name, path ("" not found)
    std::string missing() const;  // names of the tools not found, empty when all are there
    std::string simMissing(const std::string& platform = "ihp-sg13g2") const;  // the same for runFirmware
};

struct RtlFlowConfig {
    std::string design = "design";   // ORFS nickname: the result folder name
    std::string top;                 // top module
    std::vector<std::string> verilog;
    std::string clockPort = "clk";
    double clockPeriodNs = 10.0;
    double coreUtilization = 40.0;   // percent; leaves room for fill and routing
    std::string platform = "ihp-sg13g2";
    std::string workDir;             // ORFS WORK_HOME; must not contain spaces (make)
};

struct RtlFlowResult {
    bool success = false;            // flow ran, timing met, DRC clean, LVS matched
    std::string error;
    SynthesisResult synth;
    PnRResult pnr;
    DRCResult drc;
    LVSResult lvs;
    std::string gds, reportDir;
    double seconds = 0.0;
};

// What is wrong with a run of cfg before anything starts; "" when it can go. Names reach make and Tcl, so they are checked.
std::string rtlFlowConfigError(const RtlFlowConfig& cfg, const RtlFlowTools& tools);

// The DRC deck's report (KLayout's .lyrdb) and LVS's cross-reference (.lvsdb), both read from the files the decks write
// so a finished run can be read again. A missing or incomplete file is !success with its reason in errors.
DRCResult readDrcReport(const std::string& lyrdb);
LVSResult readLvsReport(const std::string& lvsdb);

using FlowLog = std::function<void(const std::string& line, bool isError)>;
using FlowStage = std::function<void(const std::string& stage, double progress)>;  // progress 0..1

RtlFlowResult runRtlToGds(const RtlFlowConfig& cfg, const RtlFlowTools& tools, FlowLog log = nullptr,
                          FlowStage stage = nullptr, const std::atomic<bool>* cancel = nullptr);

// Firmware on the core itself (Roadmap Phase 7): Icarus runs `top` from its RTL sources, or from
// the routed netlist with the SDF OpenROAD writes from the layout, inside a testbench holding the
// memory map of soc/ (ROM 0x0, SRAM 0x1000_0000, UART 0x2000_0000; one word per access,
// reads after memReadDelayNs). It stops when the core raises `suspend` (ECALL) or at maxCycles.
// The core's ports are those of ORFS riscv32i (clk reset instr readdata pc aluout writedata
// memwrite memread suspend).
struct FirmwareSimConfig {
    std::string top = "riscv";
    std::vector<std::string> rtl;   // RTL sources: functional run; empty: the routed netlist
    std::string results;            // ORFS results dir of the routed design (6_final.v/.odb/.sdc)
    std::string platform = "ihp-sg13g2";
    std::vector<uint32_t> rom;      // firmware, word 0 at address 0
    double clockPeriodNs = 10.0;
    double memReadDelayNs = 0.0;    // ROM and SRAM read time (soc/: the SPICE-characterised SRAM)
    long maxCycles = 100000;
    std::string workDir;
};

struct FirmwareSimResult {
    bool ok = false;      // ran and halted on ECALL
    std::string error;
    std::string uart;     // bytes written to 0x2000_0000
    long cycles = 0;      // clock cycles from reset release to the ECALL
    bool timedOut = false;
    double seconds = 0.0;
};

// The routed design's shortest clock period by STA (OpenROAD, global-route parasitics): every
// input and output budgeted ioDelayNs of the period at its far side. < 0 with `error` on failure.
double staMinPeriodNs(const std::string& results, const std::string& platform, const RtlFlowTools& tools,
                      double ioDelayNs, std::string& error);

FirmwareSimResult runFirmware(const FirmwareSimConfig& cfg, const RtlFlowTools& tools, FlowLog log = nullptr,
                              const std::atomic<bool>* cancel = nullptr);

// A testbench of the designer's on the RTL, or on the routed netlist with the delays of its layout. Icarus runs it with every
// signal recorded. It passes only when it finishes by itself, prints passPattern, and reports no line starting FAIL, ERROR
// or FATAL. ponytail: Icarus does not run timing checks, so a setup violation shows up only as a wrong value downstream.
struct TestbenchConfig {
    std::string top;                     // the testbench's module
    std::vector<std::string> testbench;  // its files
    std::vector<std::string> rtl;        // the design's RTL: functional run; empty: the routed netlist in `results`
    std::string results;                 // ORFS results dir of the routed design
    std::string platform;
    std::string dut;                     // gate level: the design's instance, as tb.dut, for the delays
    std::vector<std::string> defines;    // NAME or NAME=VALUE
    std::string passPattern = "PASS";
    double timeLimitNs = 1e6;
    std::string workDir;
};
struct TestbenchResult {
    bool ran = false, passed = false;
    std::string error;                   // why it did not pass; "" when it did
    std::string output, vcd;
    double seconds = 0.0;
};
TestbenchResult runTestbench(const TestbenchConfig& cfg, const RtlFlowTools& tools, FlowLog log = nullptr,
                             const std::atomic<bool>* cancel = nullptr);

}  // namespace acutesim::compute::orchestration
