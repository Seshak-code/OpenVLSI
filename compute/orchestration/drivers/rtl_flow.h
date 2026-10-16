#pragma once
// RTL -> GDS on a real PDK (Roadmap Phase 6): OpenROAD-flow-scripts (Yosys, OpenROAD) for
// synthesis through routing and fill, then the PDK's own KLayout decks for DRC and LVS.
// The flow is ORFS's maintained, taped-out recipe; this file only writes the design config,
// runs it and reads its reports into the result structs the GUI dialogs show. Qt-free.
#include "yosys_log_parser.h"        // SynthesisResult
#include "openroad_report_parser.h"  // PnRResult
#include "klayout_report_parser.h"   // DRCResult, LVSResult

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace acutesim::compute::orchestration {

struct RtlFlowTools {
    std::string flowDir;   // ORFS flow/ (holds Makefile and platforms/)
    std::string make, yosys, openroad, klayout;
    std::string binDir;    // prepended to PATH (the klayout wrapper, a pmap shim for IHP's LVS deck)

    // ORFS_FLOW_HOME / OPENROAD_EXE / KLAYOUT_CMD / YOSYS_EXE override; else ~/eda (where the
    // source builds live: their build tools break on paths with spaces), else PATH
    static RtlFlowTools detect();
    std::string missing() const;  // names of the tools not found, empty when all are there
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

}  // namespace acutesim::compute::orchestration
