#include "tool_driver.h"
#include "../server/subprocess.h"
#include <QTemporaryFile>
#include <QDir>
#include <QProcess>
#include <thread>

namespace acutesim::compute::orchestration {

using acutesim::orchestration::Subprocess;
using acutesim::orchestration::SubprocessResult;

// ============================================================================
// UTILITY
// ============================================================================

QString ToolDriver::writeTempScript(const QString& content, const QString& suffix) {
    QTemporaryFile tmpFile(QDir::tempPath() + "/acutesim_XXXXXX" + suffix);
    tmpFile.setAutoRemove(false);  // Keep alive for subprocess
    if (tmpFile.open()) {
        tmpFile.write(content.toUtf8());
        tmpFile.close();
        return tmpFile.fileName();
    }

    return {};
}

// Static mock state
static bool s_mockMode = false;

void ToolDriver::setMockMode(bool enabled) {
    s_mockMode = enabled;
}

bool ToolDriver::isMockMode() {
    return s_mockMode;
}

bool ToolDriver::isToolAvailable(const QString& toolName) {
    if (s_mockMode) return true;
    
    QProcess proc;
    proc.start("which", {toolName});
    proc.waitForFinished(3000);
    return proc.exitCode() == 0;
}

// ============================================================================
// YOSYS
// ============================================================================

SynthesisResult ToolDriver::runYosys(
    const SynthesisConfig& config,
    const QString& workDir,
    const QString& yosysPath,
    LogCallback onLog,
    std::chrono::milliseconds timeout) {
    
    // --- MOCK MODE ---
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
        
        QString mockOutput = R"(
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
    
    // 1. Generate TCL script
    QString tcl = YosysTask::generateTcl(config);
    QString scriptPath = writeTempScript(tcl, ".tcl");
    
    if (scriptPath.isEmpty()) {
        SynthesisResult result;
        result.success = false;
        result.errors.append("Failed to create temporary script file");
        return result;
    }
    
    // 2. Execute via Subprocess
    Subprocess proc(yosysPath.toStdString(), {"-s", scriptPath.toStdString()});
    proc.setWorkingDirectory(workDir.toStdString());
    proc.setTimeout(timeout);
    
    SubprocessResult subResult;
    if (onLog) {
        subResult = proc.runWithStreaming(
            [&onLog](const std::string& line) {
                onLog(QString::fromStdString(line), false);
            },
            [&onLog](const std::string& line) {
                onLog(QString::fromStdString(line), true);
            }
        );
    } else {
        subResult = proc.run();
    }
    
    // 3. Parse output
    QString fullOutput = QString::fromStdString(subResult.stdout_output);
    SynthesisResult result = YosysLogParser::parse(fullOutput);
    
    // Merge stderr errors
    if (!subResult.stderr_output.empty()) {
        YosysLogParser::parseErrors(
            QString::fromStdString(subResult.stderr_output), result);
    }
    
    // Check process-level failure
    if (subResult.exit_code != 0) {
        result.success = false;
        if (subResult.timed_out) {
            result.errors.append(QString("Yosys timed out after %1ms")
                .arg(timeout.count()));
        }
    }
    
    // 4. Cleanup temp file
    QFile::remove(scriptPath);
    
    return result;
}

// ============================================================================
// OPENROAD (Full Flow)
// ============================================================================

PnRResult ToolDriver::runOpenROAD(
    const FloorplanConfig& fpConfig,
    const PlacementConfig& placeConfig,
    const CTSConfig& ctsConfig,
    const RoutingConfig& routeConfig,
    const QString& workDir,
    const QString& openroadPath,
    LogCallback onLog,
    std::chrono::milliseconds timeout) {
    
    // --- MOCK MODE ---
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
        
        // Generate Mock DEF for Overlay Visualization
        QList<SchematicPlacement> mockPlacements;
        // Place cells in a visible pattern
        mockPlacements.append({"U1", "sky130_fd_sc_hd__inv_1", QPointF(50, 50), 0.0, false});
        mockPlacements.append({"U2", "sky130_fd_sc_hd__nand2_1", QPointF(150, 50), 0.0, false});
        mockPlacements.append({"U3", "sky130_fd_sc_hd__dfrtp_1", QPointF(100, 150), 90.0, false});
        
        // Create demo DEF file
        QString defContent = DEFTranslator::generateDEF(
            fpConfig.designName.isEmpty() ? "mock_design" : fpConfig.designName,
            mockPlacements, 
            fpConfig.dieArea.isEmpty() ? QSizeF(200, 200) : fpConfig.dieArea
        );
        
        QString defPath = workDir + "/" + (fpConfig.designName.isEmpty() ? "mock_design" : fpConfig.designName) + ".def";
        QFile defFile(defPath);
        if (defFile.open(QIODevice::WriteOnly)) {
            defFile.write(defContent.toUtf8());
            defFile.close();
            if (onLog) onLog("Generated mock DEF: " + defPath, false);
        }
        
        QString mockOutput = R"(
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
    
    // 1. Generate full-flow TCL
    QString tcl = OpenRoadTask::generateFullFlowTcl(fpConfig, placeConfig, ctsConfig, routeConfig);
    QString scriptPath = writeTempScript(tcl, ".tcl");
    
    if (scriptPath.isEmpty()) {
        PnRResult result;
        result.success = false;
        result.errors.append("Failed to create temporary script file");
        return result;
    }
    
    // 2. Execute
    Subprocess proc(openroadPath.toStdString(), {"-no_init", scriptPath.toStdString()});
    proc.setWorkingDirectory(workDir.toStdString());
    proc.setTimeout(timeout);
    
    SubprocessResult subResult;
    if (onLog) {
        subResult = proc.runWithStreaming(
            [&onLog](const std::string& line) {
                onLog(QString::fromStdString(line), false);
            },
            [&onLog](const std::string& line) {
                onLog(QString::fromStdString(line), true);
            }
        );
    } else {
        subResult = proc.run();
    }
    
    // 3. Parse
    QString fullOutput = QString::fromStdString(subResult.stdout_output);
    PnRResult result = OpenRoadReportParser::parse(fullOutput);
    
    if (!subResult.stderr_output.empty()) {
        OpenRoadReportParser::parseErrors(
            QString::fromStdString(subResult.stderr_output), result);
    }
    
    if (subResult.exit_code != 0) {
        result.success = false;
        if (subResult.timed_out) {
            result.errors.append(QString("OpenROAD timed out after %1ms")
                .arg(timeout.count()));
        }
    }
    
    QFile::remove(scriptPath);
    return result;
}

// ============================================================================
// OPENROAD (Placement Only)
// ============================================================================

PnRResult ToolDriver::runOpenROADPlacement(
    const FloorplanConfig& fpConfig,
    const PlacementConfig& placeConfig,
    const QString& workDir,
    const QString& openroadPath,
    LogCallback onLog,
    std::chrono::milliseconds timeout) {
    
    // Generate floorplan + placement TCL only
    QString tcl;
    QTextStream out(&tcl);
    out << OpenRoadTask::generateFloorplanTcl(fpConfig) << "\n\n";
    out << OpenRoadTask::generatePlacementTcl(placeConfig) << "\n\n";
    out << "report_design_area\n";
    out << "write_def " << fpConfig.designName << "_placed.def\n";
    
    QString scriptPath = writeTempScript(tcl, ".tcl");
    if (scriptPath.isEmpty()) {
        PnRResult result;
        result.success = false;
        result.errors.append("Failed to create temporary script file");
        return result;
    }
    
    Subprocess proc(openroadPath.toStdString(), {"-no_init", scriptPath.toStdString()});
    proc.setWorkingDirectory(workDir.toStdString());
    proc.setTimeout(timeout);
    
    SubprocessResult subResult;
    if (onLog) {
        subResult = proc.runWithStreaming(
            [&onLog](const std::string& line) { onLog(QString::fromStdString(line), false); },
            [&onLog](const std::string& line) { onLog(QString::fromStdString(line), true); }
        );
    } else {
        subResult = proc.run();
    }
    
    QString fullOutput = QString::fromStdString(subResult.stdout_output);
    PnRResult result = OpenRoadReportParser::parse(fullOutput);
    
    if (subResult.exit_code != 0) {
        result.success = false;
    }
    
    QFile::remove(scriptPath);
    return result;
}

// ============================================================================
// OPENROAD (Routing Only)
// ============================================================================

PnRResult ToolDriver::runOpenROADRouting(
    const RoutingConfig& routeConfig,
    const QString& placedDef,
    const QString& workDir,
    const QString& openroadPath,
    LogCallback onLog,
    std::chrono::milliseconds timeout) {
    
    QString tcl;
    QTextStream out(&tcl);
    out << "read_def " << placedDef << "\n\n";
    out << OpenRoadTask::generateRoutingTcl(routeConfig) << "\n\n";
    out << "report_worst_slack -max\n";
    out << "report_power\n";
    out << "write_def " << routeConfig.designName << "_routed.def\n";
    
    QString scriptPath = writeTempScript(tcl, ".tcl");
    if (scriptPath.isEmpty()) {
        PnRResult result;
        result.success = false;
        result.errors.append("Failed to create temporary script file");
        return result;
    }
    
    Subprocess proc(openroadPath.toStdString(), {"-no_init", scriptPath.toStdString()});
    proc.setWorkingDirectory(workDir.toStdString());
    proc.setTimeout(timeout);
    
    SubprocessResult subResult;
    if (onLog) {
        subResult = proc.runWithStreaming(
            [&onLog](const std::string& line) { onLog(QString::fromStdString(line), false); },
            [&onLog](const std::string& line) { onLog(QString::fromStdString(line), true); }
        );
    } else {
        subResult = proc.run();
    }
    
    QString fullOutput = QString::fromStdString(subResult.stdout_output);
    PnRResult result = OpenRoadReportParser::parse(fullOutput);
    
    if (subResult.exit_code != 0) {
        result.success = false;
    }
    
    QFile::remove(scriptPath);
    return result;
}

// ============================================================================
// DEF TRANSLATION (pure logic, no subprocess)
// ============================================================================

QString ToolDriver::generateDEF(
    const QString& designName,
    const QList<SchematicPlacement>& placements,
    const QSizeF& dieArea,
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
    const QString& workDir,
    const QString& klayoutPath,
    LogCallback onLog,
    std::chrono::milliseconds timeout) {
    
    // --- MOCK MODE ---
    if (s_mockMode) {
        if (onLog) {
            onLog("--- MOCK KLAYOUT DRC EXECUTION ---", false);
            onLog("Loading layout...", false);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            onLog("Running DRC rules...", false);
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            onLog("Checking metal1.width...", false);
            onLog("Checking metal1.spacing...", false);
            onLog("Checking via1.enclosure...", false);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            onLog("DRC complete.", false);
        }
        
        QString mockOutput = R"(
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
    
    // 1. Generate DRC script
    QString script = KLayoutTask::generateDRCScript(config);
    QString scriptPath = writeTempScript(script, ".rb");
    
    if (scriptPath.isEmpty()) {
        DRCResult result;
        result.success = false;
        result.errors.append("Failed to create temporary DRC script file");
        return result;
    }
    
    // 2. Execute KLayout in batch mode
    Subprocess proc(klayoutPath.toStdString(), {"-b", "-r", scriptPath.toStdString()});
    proc.setWorkingDirectory(workDir.toStdString());
    proc.setTimeout(timeout);
    
    SubprocessResult subResult;
    if (onLog) {
        subResult = proc.runWithStreaming(
            [&onLog](const std::string& line) { onLog(QString::fromStdString(line), false); },
            [&onLog](const std::string& line) { onLog(QString::fromStdString(line), true); }
        );
    } else {
        subResult = proc.run();
    }
    
    // 3. Parse output
    QString fullOutput = QString::fromStdString(subResult.stdout_output);
    DRCResult result = KLayoutReportParser::parseDRC(fullOutput);
    
    if (!subResult.stderr_output.empty()) {
        KLayoutReportParser::parseDRCErrors(
            QString::fromStdString(subResult.stderr_output), result);
    }
    
    if (subResult.exit_code != 0) {
        result.success = false;
        if (subResult.timed_out) {
            result.errors.append(QString("KLayout DRC timed out after %1ms")
                .arg(timeout.count()));
        }
    }
    
    QFile::remove(scriptPath);
    return result;
}

// ============================================================================
// KLAYOUT LVS
// ============================================================================

LVSResult ToolDriver::runKLayoutLVS(
    const LVSConfig& config,
    const QString& workDir,
    const QString& klayoutPath,
    LogCallback onLog,
    std::chrono::milliseconds timeout) {
    
    // --- MOCK MODE ---
    if (s_mockMode) {
        if (onLog) {
            onLog("--- MOCK KLAYOUT LVS EXECUTION ---", false);
            onLog("Extracting layout netlist...", false);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            onLog("Comparing with schematic netlist...", false);
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            onLog("Checking devices...", false);
            onLog("Checking nets...", false);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            onLog("LVS complete.", false);
        }
        
        QString mockOutput = R"(
KLayout LVS Report
==================
Layout devices: 8 | Schematic devices: 8
Layout nets: 12 | Schematic nets: 12
Total mismatches: 0
MATCH — Layout matches schematic.
        )";
        
        return KLayoutReportParser::parseLVS(mockOutput);
    }
    
    // 1. Generate LVS script
    QString script = KLayoutTask::generateLVSScript(config);
    QString scriptPath = writeTempScript(script, ".rb");
    
    if (scriptPath.isEmpty()) {
        LVSResult result;
        result.success = false;
        result.errors.append("Failed to create temporary LVS script file");
        return result;
    }
    
    // 2. Execute
    Subprocess proc(klayoutPath.toStdString(), {"-b", "-r", scriptPath.toStdString()});
    proc.setWorkingDirectory(workDir.toStdString());
    proc.setTimeout(timeout);
    
    SubprocessResult subResult;
    if (onLog) {
        subResult = proc.runWithStreaming(
            [&onLog](const std::string& line) { onLog(QString::fromStdString(line), false); },
            [&onLog](const std::string& line) { onLog(QString::fromStdString(line), true); }
        );
    } else {
        subResult = proc.run();
    }
    
    // 3. Parse
    QString fullOutput = QString::fromStdString(subResult.stdout_output);
    LVSResult result = KLayoutReportParser::parseLVS(fullOutput);
    
    if (!subResult.stderr_output.empty()) {
        KLayoutReportParser::parseLVSErrors(
            QString::fromStdString(subResult.stderr_output), result);
    }
    
    if (subResult.exit_code != 0) {
        result.success = false;
        if (subResult.timed_out) {
            result.errors.append(QString("KLayout LVS timed out after %1ms")
                .arg(timeout.count()));
        }
    }
    
    QFile::remove(scriptPath);
    return result;
}

} // namespace acutesim::compute::orchestration
