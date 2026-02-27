#include "tool_driver.h"
#include "../server/subprocess.h"
#include <fstream>
#include <cstdlib>
#include <thread>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#endif

namespace acutesim::compute::orchestration {

using acutesim::orchestration::Subprocess;
using acutesim::orchestration::SubprocessResult;

// ============================================================================
// UTILITY
// ============================================================================

std::string ToolDriver::writeTempScript(const std::string& content, const std::string& suffix) {
#ifdef _WIN32
    char tmpDir[MAX_PATH];
    GetTempPathA(MAX_PATH, tmpDir);
    char tmpFile[MAX_PATH];
    GetTempFileNameA(tmpDir, "acu", 0, tmpFile);
    std::string path = std::string(tmpFile) + suffix;
    // Rename with suffix
    std::rename(tmpFile, path.c_str());
#else
    std::string tmpl = "/tmp/acutesim_XXXXXX";
    // mkstemp gives us a unique fd; we use its name + rename with suffix
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    int fd = mkstemp(buf.data());
    if (fd == -1) return {};
    close(fd);
    std::string path = std::string(buf.data()) + suffix;
    std::rename(buf.data(), path.c_str());
#endif

    std::ofstream ofs(path);
    if (!ofs.is_open()) return {};
    ofs << content;
    return path;
}

static bool s_mockMode = false;

void ToolDriver::setMockMode(bool enabled) { s_mockMode = enabled; }
bool ToolDriver::isMockMode() { return s_mockMode; }

bool ToolDriver::isToolAvailable(const std::string& toolName) {
    if (s_mockMode) return true;
#ifdef EMSCRIPTEN
    return false;
#else
    Subprocess proc("which", {toolName});
    proc.setTimeout(std::chrono::milliseconds(3000));
    auto result = proc.run();
    return result.exit_code == 0;
#endif
}

// ============================================================================
// YOSYS
// ============================================================================

SynthesisResult ToolDriver::runYosys(
    const SynthesisConfig& config,
    const std::string& workDir,
    const std::string& yosysPath,
    LogCallback onLog,
    std::chrono::milliseconds timeout) {

    if (s_mockMode) {
        if (onLog) {
            onLog("--- MOCK YOSYS EXECUTION (No tools installed) ---", false);
            onLog("Reading Verilog...", false);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            onLog("Synthesizing...", false);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            onLog("Mapping to sky130_fd_sc_hd...", false);
            onLog("Printing statistics...", false);
        }

        static const std::string mockOutput = R"(
=== top ===

   Number of wires:                 12
   Number of wire bits:             12
   Number of public wires:          4
   Number of cells:                 8
     $_NOT_                          2
     $_DFF_P_                        4
     sky130_fd_sc_hd__nand2_1        2

   Chip area for module '\top': 45.120000
        )";

        return YosysLogParser::parse(mockOutput);
    }

#ifdef EMSCRIPTEN
    SynthesisResult result;
    result.success = false;
    result.errors.push_back("Yosys not supported in WASM");
    return result;
#else
    std::string tcl = YosysTask::generateTcl(config);
    std::string scriptPath = writeTempScript(tcl, ".tcl");

    if (scriptPath.empty()) {
        SynthesisResult result;
        result.success = false;
        result.errors.push_back("Failed to create temporary script file");
        return result;
    }

    Subprocess proc(yosysPath, {"-s", scriptPath});
    proc.setWorkingDirectory(workDir);
    proc.setTimeout(timeout);

    SubprocessResult subResult;
    if (onLog) {
        subResult = proc.runWithStreaming(
            [&onLog](const std::string& line) { onLog(line, false); },
            [&onLog](const std::string& line) { onLog(line, true); });
    } else {
        subResult = proc.run();
    }

    SynthesisResult result = YosysLogParser::parse(subResult.stdout_output);
    if (!subResult.stderr_output.empty())
        YosysLogParser::parseErrors(subResult.stderr_output, result);

    if (subResult.exit_code != 0) {
        result.success = false;
        if (subResult.timed_out)
            result.errors.push_back("Yosys timed out after " + std::to_string(timeout.count()) + "ms");
    }

    std::remove(scriptPath.c_str());
    return result;
#endif
}

// ============================================================================
// OPENROAD (Full Flow)
// ============================================================================

PnRResult ToolDriver::runOpenROAD(
    const FloorplanConfig& fpConfig,
    const PlacementConfig& placeConfig,
    const CTSConfig& ctsConfig,
    const RoutingConfig& routeConfig,
    const std::string& workDir,
    const std::string& openroadPath,
    LogCallback onLog,
    std::chrono::milliseconds timeout) {

    if (s_mockMode) {
        if (onLog) {
            onLog("--- MOCK OPENROAD EXECUTION ---", false);
            onLog("Phase 1: Floorplanning...", false);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            onLog("Phase 2: Global Placement...", false);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            onLog("Phase 3: CTS...", false);
            onLog("Phase 4: Detailed Routing...", false);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            onLog("Writing DEF...", false);
        }

        std::vector<SchematicPlacement> mockPlacements;
        mockPlacements.push_back({"U1", "sky130_fd_sc_hd__inv_1",   {50.0, 50.0},   0.0, false});
        mockPlacements.push_back({"U2", "sky130_fd_sc_hd__nand2_1", {150.0, 50.0},  0.0, false});
        mockPlacements.push_back({"U3", "sky130_fd_sc_hd__dfrtp_1", {100.0, 150.0}, 90.0, false});

        Size2D dieSize = fpConfig.dieArea.isEmpty() ? Size2D{200.0, 200.0} : fpConfig.dieArea;
        std::string designName = fpConfig.designName.empty() ? "mock_design" : fpConfig.designName;

        std::string defContent = DEFTranslator::generateDEF(designName, mockPlacements, dieSize);
        std::string defPath = workDir + "/" + designName + ".def";
        std::ofstream defFile(defPath);
        if (defFile.is_open()) {
            defFile << defContent;
            if (onLog) onLog("Generated mock DEF: " + defPath, false);
        }

        static const std::string mockOutput = R"(
OpenROAD v2.0
Design area 200.0 u^2 65% utilization.
worst slack 0.150
tns 0.000
Total Power     = 1.5 mW
Dynamic Power   = 1.2 mW
Leakage Power   = 0.3 mW
Internal Power  = 0.8 mW
Number of routed nets: 42
DRC violations: 0
Antenna violations: 0
        )";

        return OpenRoadReportParser::parse(mockOutput);
    }

#ifdef EMSCRIPTEN
    PnRResult result;
    result.success = false;
    result.errors.push_back("OpenROAD not supported in WASM");
    return result;
#else
    std::string tcl = OpenRoadTask::generateFullFlowTcl(fpConfig, placeConfig, ctsConfig, routeConfig);
    std::string scriptPath = writeTempScript(tcl, ".tcl");

    if (scriptPath.empty()) {
        PnRResult result;
        result.success = false;
        result.errors.push_back("Failed to create temporary script file");
        return result;
    }

    Subprocess proc(openroadPath, {"-no_init", scriptPath});
    proc.setWorkingDirectory(workDir);
    proc.setTimeout(timeout);

    SubprocessResult subResult;
    if (onLog) {
        subResult = proc.runWithStreaming(
            [&onLog](const std::string& line) { onLog(line, false); },
            [&onLog](const std::string& line) { onLog(line, true); });
    } else {
        subResult = proc.run();
    }

    PnRResult result = OpenRoadReportParser::parse(subResult.stdout_output);
    if (!subResult.stderr_output.empty())
        OpenRoadReportParser::parseErrors(subResult.stderr_output, result);

    if (subResult.exit_code != 0) {
        result.success = false;
        if (subResult.timed_out)
            result.errors.push_back("OpenROAD timed out after " + std::to_string(timeout.count()) + "ms");
    }

    std::remove(scriptPath.c_str());
    return result;
#endif
}

// ============================================================================
// OPENROAD (Placement Only)
// ============================================================================

PnRResult ToolDriver::runOpenROADPlacement(
    const FloorplanConfig& fpConfig,
    const PlacementConfig& placeConfig,
    const std::string& workDir,
    const std::string& openroadPath,
    LogCallback onLog,
    std::chrono::milliseconds timeout) {

    std::string tcl =
        OpenRoadTask::generateFloorplanTcl(fpConfig) + "\n\n" +
        OpenRoadTask::generatePlacementTcl(placeConfig) + "\n\n" +
        "report_design_area\n" +
        "write_def " + fpConfig.designName + "_placed.def\n";

    std::string scriptPath = writeTempScript(tcl, ".tcl");
    if (scriptPath.empty()) {
        PnRResult result;
        result.success = false;
        result.errors.push_back("Failed to create temporary script file");
        return result;
    }

    Subprocess proc(openroadPath, {"-no_init", scriptPath});
    proc.setWorkingDirectory(workDir);
    proc.setTimeout(timeout);

    SubprocessResult subResult;
    if (onLog) {
        subResult = proc.runWithStreaming(
            [&onLog](const std::string& line) { onLog(line, false); },
            [&onLog](const std::string& line) { onLog(line, true); });
    } else {
        subResult = proc.run();
    }

    PnRResult result = OpenRoadReportParser::parse(subResult.stdout_output);
    if (subResult.exit_code != 0) result.success = false;

    std::remove(scriptPath.c_str());
    return result;
}

// ============================================================================
// OPENROAD (Routing Only)
// ============================================================================

PnRResult ToolDriver::runOpenROADRouting(
    const RoutingConfig& routeConfig,
    const std::string& placedDef,
    const std::string& workDir,
    const std::string& openroadPath,
    LogCallback onLog,
    std::chrono::milliseconds timeout) {

    std::string tcl =
        "read_def " + placedDef + "\n\n" +
        OpenRoadTask::generateRoutingTcl(routeConfig) + "\n\n" +
        "report_worst_slack -max\n" +
        "report_power\n" +
        "write_def " + routeConfig.designName + "_routed.def\n";

    std::string scriptPath = writeTempScript(tcl, ".tcl");
    if (scriptPath.empty()) {
        PnRResult result;
        result.success = false;
        result.errors.push_back("Failed to create temporary script file");
        return result;
    }

    Subprocess proc(openroadPath, {"-no_init", scriptPath});
    proc.setWorkingDirectory(workDir);
    proc.setTimeout(timeout);

    SubprocessResult subResult;
    if (onLog) {
        subResult = proc.runWithStreaming(
            [&onLog](const std::string& line) { onLog(line, false); },
            [&onLog](const std::string& line) { onLog(line, true); });
    } else {
        subResult = proc.run();
    }

    PnRResult result = OpenRoadReportParser::parse(subResult.stdout_output);
    if (subResult.exit_code != 0) result.success = false;

    std::remove(scriptPath.c_str());
    return result;
}

// ============================================================================
// DEF TRANSLATION (pure logic, no subprocess)
// ============================================================================

std::string ToolDriver::generateDEF(
    const std::string& designName,
    const std::vector<SchematicPlacement>& placements,
    const Size2D& dieArea,
    double pixelToMicronScale,
    double canvasHeight,
    int defUnitsPerMicron) {

    return DEFTranslator::generateDEF(designName, placements, dieArea,
                                       pixelToMicronScale, canvasHeight,
                                       defUnitsPerMicron);
}

// ============================================================================
// KLAYOUT DRC
// ============================================================================

DRCResult ToolDriver::runKLayoutDRC(
    const DRCConfig& config,
    const std::string& workDir,
    const std::string& klayoutPath,
    LogCallback onLog,
    std::chrono::milliseconds timeout) {

    if (s_mockMode) {
        if (onLog) {
            onLog("--- MOCK KLAYOUT DRC EXECUTION ---", false);
            onLog("Loading layout...", false);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            onLog("Running DRC rules...", false);
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            onLog("DRC complete.", false);
        }

        static const std::string mockOutput = R"(
KLayout DRC Report
==================
Rule: metal1.width | Category: Width | Layer: metal1 | Violations: 0
Rule: metal1.spacing | Category: Spacing | Layer: metal1 | Violations: 0
Rule: via1.enclosure | Category: Enclosure | Layer: via1 | Violations: 0
Total DRC violations: 0
DRC clean
        )";

        return KLayoutReportParser::parseDRC(mockOutput);
    }

#ifdef EMSCRIPTEN
    DRCResult result;
    result.success = false;
    result.errors.push_back("KLayout not supported in WASM");
    return result;
#else
    std::string script = KLayoutTask::generateDRCScript(config);
    std::string scriptPath = writeTempScript(script, ".rb");

    if (scriptPath.empty()) {
        DRCResult result;
        result.success = false;
        result.errors.push_back("Failed to create temporary DRC script file");
        return result;
    }

    Subprocess proc(klayoutPath, {"-b", "-r", scriptPath});
    proc.setWorkingDirectory(workDir);
    proc.setTimeout(timeout);

    SubprocessResult subResult;
    if (onLog) {
        subResult = proc.runWithStreaming(
            [&onLog](const std::string& line) { onLog(line, false); },
            [&onLog](const std::string& line) { onLog(line, true); });
    } else {
        subResult = proc.run();
    }

    DRCResult result = KLayoutReportParser::parseDRC(subResult.stdout_output);
    if (!subResult.stderr_output.empty())
        KLayoutReportParser::parseDRCErrors(subResult.stderr_output, result);

    if (subResult.exit_code != 0) {
        result.success = false;
        if (subResult.timed_out)
            result.errors.push_back("KLayout DRC timed out after " + std::to_string(timeout.count()) + "ms");
    }

    std::remove(scriptPath.c_str());
    return result;
#endif
}

// ============================================================================
// KLAYOUT LVS
// ============================================================================

LVSResult ToolDriver::runKLayoutLVS(
    const LVSConfig& config,
    const std::string& workDir,
    const std::string& klayoutPath,
    LogCallback onLog,
    std::chrono::milliseconds timeout) {

    if (s_mockMode) {
        if (onLog) {
            onLog("--- MOCK KLAYOUT LVS EXECUTION ---", false);
            onLog("Extracting layout netlist...", false);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            onLog("Comparing with schematic netlist...", false);
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            onLog("LVS complete.", false);
        }

        static const std::string mockOutput = R"(
KLayout LVS Report
==================
Layout devices: 8 | Schematic devices: 8
Layout nets: 12 | Schematic nets: 12
Total mismatches: 0
MATCH — Layout matches schematic.
        )";

        return KLayoutReportParser::parseLVS(mockOutput);
    }

#ifdef EMSCRIPTEN
    LVSResult result;
    result.success = false;
    result.errors.push_back("KLayout not supported in WASM");
    return result;
#else
    std::string script = KLayoutTask::generateLVSScript(config);
    std::string scriptPath = writeTempScript(script, ".rb");

    if (scriptPath.empty()) {
        LVSResult result;
        result.success = false;
        result.errors.push_back("Failed to create temporary LVS script file");
        return result;
    }

    Subprocess proc(klayoutPath, {"-b", "-r", scriptPath});
    proc.setWorkingDirectory(workDir);
    proc.setTimeout(timeout);

    SubprocessResult subResult;
    if (onLog) {
        subResult = proc.runWithStreaming(
            [&onLog](const std::string& line) { onLog(line, false); },
            [&onLog](const std::string& line) { onLog(line, true); });
    } else {
        subResult = proc.run();
    }

    LVSResult result = KLayoutReportParser::parseLVS(subResult.stdout_output);
    if (!subResult.stderr_output.empty())
        KLayoutReportParser::parseLVSErrors(subResult.stderr_output, result);

    if (subResult.exit_code != 0) {
        result.success = false;
        if (subResult.timed_out)
            result.errors.push_back("KLayout LVS timed out after " + std::to_string(timeout.count()) + "ms");
    }

    std::remove(scriptPath.c_str());
    return result;
#endif
}

// ============================================================================
// KLAYOUT PEX
// ============================================================================

DRCResult ToolDriver::runKLayoutPEX(
    const PEXConfig& config,
    const std::string& workDir,
    const std::string& klayoutPath,
    LogCallback onLog,
    std::chrono::milliseconds timeout) {

    if (s_mockMode) {
        if (onLog) {
            onLog("--- MOCK KLAYOUT PEX EXECUTION ---", false);
            onLog("Extracting parasitics...", false);
            onLog("Writing SPEF...", false);
        }
        DRCResult r;
        r.success = true;
        return r;
    }

#ifdef EMSCRIPTEN
    DRCResult result;
    result.success = false;
    result.errors.push_back("KLayout not supported in WASM");
    return result;
#else
    std::string script = KLayoutTask::generatePEXScript(config);
    std::string scriptPath = writeTempScript(script, ".rb");
    if (scriptPath.empty()) {
        DRCResult result;
        result.success = false;
        result.errors.push_back("Failed to create temporary PEX script file");
        return result;
    }

    Subprocess proc(klayoutPath, {"-b", "-r", scriptPath});
    proc.setWorkingDirectory(workDir);
    proc.setTimeout(timeout);

    SubprocessResult subResult;
    if (onLog) {
        subResult = proc.runWithStreaming(
            [&onLog](const std::string& line) { onLog(line, false); },
            [&onLog](const std::string& line) { onLog(line, true); });
    } else {
        subResult = proc.run();
    }

    DRCResult result = KLayoutReportParser::parseDRC(subResult.stdout_output);
    if (subResult.exit_code != 0) {
        result.success = false;
        if (subResult.timed_out)
            result.errors.push_back("KLayout PEX timed out after " + std::to_string(timeout.count()) + "ms");
    }

    std::remove(scriptPath.c_str());
    return result;
#endif
}

// ============================================================================
// OPENROAD RCX
// ============================================================================

PnRResult ToolDriver::runOpenROADRCX(
    const RCXConfig& config,
    const std::string& workDir,
    const std::string& openroadPath,
    LogCallback onLog,
    std::chrono::milliseconds timeout) {

    if (s_mockMode) {
        if (onLog) {
            onLog("--- MOCK OPENROAD RCX EXECUTION ---", false);
            onLog("Loading routed DEF...", false);
            onLog("Running parasitic extraction...", false);
            onLog("Writing SPEF: " + config.outputSpef, false);
        }
        PnRResult r;
        r.success = true;
        return r;
    }

#ifdef EMSCRIPTEN
    PnRResult result;
    result.success = false;
    result.errors.push_back("OpenROAD RCX not supported in WASM");
    return result;
#else
    std::string tcl = OpenRoadTask::generateRCXTcl(config);
    std::string scriptPath = writeTempScript(tcl, ".tcl");
    if (scriptPath.empty()) {
        PnRResult result;
        result.success = false;
        result.errors.push_back("Failed to create temporary RCX script file");
        return result;
    }

    Subprocess proc(openroadPath, {"-no_init", scriptPath});
    proc.setWorkingDirectory(workDir);
    proc.setTimeout(timeout);

    SubprocessResult subResult;
    if (onLog) {
        subResult = proc.runWithStreaming(
            [&onLog](const std::string& line) { onLog(line, false); },
            [&onLog](const std::string& line) { onLog(line, true); });
    } else {
        subResult = proc.run();
    }

    PnRResult result = OpenRoadReportParser::parse(subResult.stdout_output);
    if (subResult.exit_code != 0) {
        result.success = false;
        if (subResult.timed_out)
            result.errors.push_back("OpenROAD RCX timed out after " + std::to_string(timeout.count()) + "ms");
    }

    std::remove(scriptPath.c_str());
    return result;
#endif
}

// ============================================================================
// KLAYOUT GDSII EXPORT
// ============================================================================

DRCResult ToolDriver::runKLayoutGDSExport(
    const GDSExportConfig& config,
    const std::string& workDir,
    const std::string& klayoutPath,
    LogCallback onLog,
    std::chrono::milliseconds timeout) {

    if (s_mockMode) {
        if (onLog) {
            onLog("--- MOCK KLAYOUT GDS EXPORT ---", false);
            onLog("Exporting GDSII to: " + config.outputGDS, false);
        }
        DRCResult r;
        r.success = true;
        return r;
    }

#ifdef EMSCRIPTEN
    DRCResult result;
    result.success = false;
    result.errors.push_back("KLayout not supported in WASM");
    return result;
#else
    std::string script = KLayoutTask::generateGDSExportScript(config);
    std::string scriptPath = writeTempScript(script, ".rb");
    if (scriptPath.empty()) {
        DRCResult result;
        result.success = false;
        result.errors.push_back("Failed to create temporary GDS export script file");
        return result;
    }

    Subprocess proc(klayoutPath, {"-b", "-r", scriptPath});
    proc.setWorkingDirectory(workDir);
    proc.setTimeout(timeout);

    SubprocessResult subResult;
    if (onLog) {
        subResult = proc.runWithStreaming(
            [&onLog](const std::string& line) { onLog(line, false); },
            [&onLog](const std::string& line) { onLog(line, true); });
    } else {
        subResult = proc.run();
    }

    DRCResult result;
    result.success = (subResult.exit_code == 0);
    if (!result.success) {
        if (subResult.timed_out)
            result.errors.push_back("KLayout GDS export timed out after " + std::to_string(timeout.count()) + "ms");
        else
            result.errors.push_back(subResult.stderr_output);
    }

    std::remove(scriptPath.c_str());
    return result;
#endif
}

} // namespace acutesim::compute::orchestration
