#pragma once

#include <QString>
#include <functional>
#include <chrono>
#include "yosys_task.h"
#include "yosys_log_parser.h"
#include "openroad_task.h"
#include "openroad_report_parser.h"
#include "klayout_task.h"
#include "klayout_report_parser.h"

namespace acutesim::compute::orchestration {

/**
 * @brief Unified EDA Tool Driver — composes TCL generation, subprocess
 * execution, and log parsing into a single interface.
 *
 * This class ties together the three-layer architecture:
 *   1. Task classes (YosysTask, OpenRoadTask) generate TCL scripts
 *   2. Subprocess executes the tool in a sandboxed environment
 *   3. Log parsers extract structured results from stdout/stderr
 *
 * All methods accept an optional timeout. If a tool exceeds the timeout,
 * the subprocess is killed and the result is marked as failed.
 */
class ToolDriver {
public:
    using LogCallback = std::function<void(const QString& line, bool isError)>;
    
    // Default timeout for tool execution (10 minutes)
    static constexpr auto DEFAULT_TIMEOUT = std::chrono::minutes(10);
    
    // Mock Mode for Verification (No external tools required)
    static void setMockMode(bool enabled);
    static bool isMockMode();

    // ========================================================================
    // YOSYS SYNTHESIS
    // ========================================================================

    /**
     * Run a complete Yosys synthesis flow:
     *   1. Generate TCL from SynthesisConfig
     *   2. Write to temp file
     *   3. Execute `yosys -s <script>`
     *   4. Parse output
     *
     * @param config       Synthesis configuration
     * @param workDir      Working directory for subprocess
     * @param yosysPath    Path to Yosys executable (default: "yosys")
     * @param onLog        Optional callback for streaming log lines
     * @param timeout      Maximum execution time before kill
     * @return Structured SynthesisResult
     */
    static SynthesisResult runYosys(
        const SynthesisConfig& config,
        const QString& workDir,
        const QString& yosysPath = "yosys",
        LogCallback onLog = nullptr,
        std::chrono::milliseconds timeout = DEFAULT_TIMEOUT);

    // ========================================================================
    // OPENROAD PLACE & ROUTE
    // ========================================================================

    /**
     * Run a complete OpenROAD P&R flow:
     *   1. Generate full-flow TCL from configs
     *   2. Write to temp file
     *   3. Execute `openroad -no_init <script>`
     *   4. Parse reports
     *
     * @param fpConfig     Floorplan configuration
     * @param placeConfig  Placement configuration
     * @param ctsConfig    CTS configuration
     * @param routeConfig  Routing configuration
     * @param workDir      Working directory for subprocess
     * @param openroadPath Path to OpenROAD executable
     * @param onLog        Optional callback for streaming log lines
     * @param timeout      Maximum execution time before kill
     * @return Structured PnRResult
     */
    static PnRResult runOpenROAD(
        const FloorplanConfig& fpConfig,
        const PlacementConfig& placeConfig,
        const CTSConfig& ctsConfig,
        const RoutingConfig& routeConfig,
        const QString& workDir,
        const QString& openroadPath = "openroad",
        LogCallback onLog = nullptr,
        std::chrono::milliseconds timeout = DEFAULT_TIMEOUT);

    // ========================================================================
    // INDIVIDUAL STAGES (for incremental flow)
    // ========================================================================

    /**
     * Run only floorplan + placement in OpenROAD.
     */
    static PnRResult runOpenROADPlacement(
        const FloorplanConfig& fpConfig,
        const PlacementConfig& placeConfig,
        const QString& workDir,
        const QString& openroadPath = "openroad",
        LogCallback onLog = nullptr,
        std::chrono::milliseconds timeout = DEFAULT_TIMEOUT);

    /**
     * Run only routing in OpenROAD (assumes placement DEF exists).
     */
    static PnRResult runOpenROADRouting(
        const RoutingConfig& routeConfig,
        const QString& placedDef,
        const QString& workDir,
        const QString& openroadPath = "openroad",
        LogCallback onLog = nullptr,
        std::chrono::milliseconds timeout = DEFAULT_TIMEOUT);

    // ========================================================================
    // DEF TRANSLATION
    // ========================================================================

    /**
     * Generate a DEF file from schematic canvas positions.
     * This is a pure translation (no subprocess needed).
     */
    static QString generateDEF(
        const QString& designName,
        const QList<SchematicPlacement>& placements,
        const QSizeF& dieArea,
        double pixelToMicronScale = 0.1,
        double canvasHeight = 1000.0,
        int defUnitsPerMicron = 1000);

    // ========================================================================
    // UTILITY
    // ========================================================================

    /**
     * Write a generated script to a temp file and return the path.
     */
    static QString writeTempScript(const QString& content, const QString& suffix);

    /**
     * Check if a tool binary is available on $PATH.
     */
    static bool isToolAvailable(const QString& toolName);

    // ========================================================================
    // KLAYOUT SIGN-OFF
    // ========================================================================

    /**
     * Run KLayout DRC (Design Rule Check):
     *   1. Generate Ruby DRC script from DRCConfig
     *   2. Write to temp file
     *   3. Execute `klayout -b -r <script>`
     *   4. Parse report output
     *
     * @param config       DRC configuration
     * @param workDir      Working directory
     * @param klayoutPath  Path to KLayout executable
     * @param onLog        Optional callback for streaming log lines
     * @param timeout      Maximum execution time
     * @return Structured DRCResult
     */
    static DRCResult runKLayoutDRC(
        const DRCConfig& config,
        const QString& workDir,
        const QString& klayoutPath = "klayout",
        LogCallback onLog = nullptr,
        std::chrono::milliseconds timeout = DEFAULT_TIMEOUT);

    /**
     * Run KLayout LVS (Layout vs Schematic):
     *   1. Generate Ruby LVS script from LVSConfig
     *   2. Write to temp file
     *   3. Execute `klayout -b -r <script>`
     *   4. Parse report output
     *
     * @param config       LVS configuration
     * @param workDir      Working directory
     * @param klayoutPath  Path to KLayout executable
     * @param onLog        Optional callback for streaming log lines
     * @param timeout      Maximum execution time
     * @return Structured LVSResult
     */
    static LVSResult runKLayoutLVS(
        const LVSConfig& config,
        const QString& workDir,
        const QString& klayoutPath = "klayout",
        LogCallback onLog = nullptr,
        std::chrono::milliseconds timeout = DEFAULT_TIMEOUT);
};

} // namespace acutesim::compute::orchestration
