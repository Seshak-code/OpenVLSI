#pragma once
// RTL -> GDS on a real PDK (Roadmap Phase 6): OpenROAD-flow-scripts (Yosys, OpenROAD) for
// synthesis through routing and fill, then the PDK's own KLayout decks for DRC and LVS.
// The flow is ORFS's maintained, taped-out recipe; this file only writes the design config,
// runs it and reads its reports into the result structs the GUI dialogs show. Qt-free.
#include "yosys_log_parser.h"        // SynthesisResult
#include "openroad_report_parser.h"  // PnRResult
#include "klayout_report_parser.h"   // DRCResult, LVSResult

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace acutesim::compute::orchestration {

struct RtlFlowTools {
    std::string flowDir;   // ORFS flow/ (holds Makefile and platforms/)
    std::string make, yosys, openroad, klayout;
    std::string binDir;    // prepended to PATH (the klayout wrapper, a pmap shim for IHP's LVS deck)
    std::string iverilog, vvp;  // firmware simulation (Icarus: it annotates SDF, Verilator does not)
    std::string cellModels;     // IHP-Open-PDK sg13g2_stdcell.v + sg13g2_udp.v (~/eda/ihp-verilog)

    // ORFS_FLOW_HOME / OPENROAD_EXE / KLAYOUT_CMD / YOSYS_EXE override; else ~/eda (where the
    // source builds live: their build tools break on paths with spaces), else PATH
    static RtlFlowTools detect();
    std::string missing() const;  // names of the tools not found, empty when all are there
    std::string simMissing() const;  // the same for runFirmware
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

}  // namespace acutesim::compute::orchestration
