#pragma once

#include <string>
#include <functional>
#include <chrono>
#include <vector>
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
 * All string parameters use std::string. Qt-free.
 */
class ToolDriver {
public:
    using LogCallback = std::function<void(const std::string& line, bool isError)>;

    static constexpr auto DEFAULT_TIMEOUT = std::chrono::minutes(10);

    static void setMockMode(bool enabled);
    static bool isMockMode();

    // ========================================================================
    // YOSYS SYNTHESIS
    // ========================================================================
    static SynthesisResult runYosys(
        const SynthesisConfig& config,
        const std::string& workDir,
        const std::string& yosysPath = "yosys",
        LogCallback onLog = nullptr,
        std::chrono::milliseconds timeout = DEFAULT_TIMEOUT);

    // ========================================================================
    // OPENROAD PLACE & ROUTE
    // ========================================================================
    static PnRResult runOpenROAD(
        const FloorplanConfig& fpConfig,
        const PlacementConfig& placeConfig,
        const CTSConfig& ctsConfig,
        const RoutingConfig& routeConfig,
        const std::string& workDir,
        const std::string& openroadPath = "openroad",
        LogCallback onLog = nullptr,
        std::chrono::milliseconds timeout = DEFAULT_TIMEOUT);

    static PnRResult runOpenROADPlacement(
        const FloorplanConfig& fpConfig,
        const PlacementConfig& placeConfig,
        const std::string& workDir,
        const std::string& openroadPath = "openroad",
        LogCallback onLog = nullptr,
        std::chrono::milliseconds timeout = DEFAULT_TIMEOUT);

    static PnRResult runOpenROADRouting(
        const RoutingConfig& routeConfig,
        const std::string& placedDef,
        const std::string& workDir,
        const std::string& openroadPath = "openroad",
        LogCallback onLog = nullptr,
        std::chrono::milliseconds timeout = DEFAULT_TIMEOUT);

    // ========================================================================
    // DEF TRANSLATION
    // ========================================================================
    static std::string generateDEF(
        const std::string& designName,
        const std::vector<SchematicPlacement>& placements,
        const Size2D& dieArea,
        double pixelToMicronScale = 0.1,
        double canvasHeight = 1000.0,
        int defUnitsPerMicron = 1000);

    // ========================================================================
    // UTILITY
    // ========================================================================
    static std::string writeTempScript(const std::string& content, const std::string& suffix);
    static bool isToolAvailable(const std::string& toolName);

    // ========================================================================
    // KLAYOUT SIGN-OFF
    // ========================================================================
    static DRCResult runKLayoutDRC(
        const DRCConfig& config,
        const std::string& workDir,
        const std::string& klayoutPath = "klayout",
        LogCallback onLog = nullptr,
        std::chrono::milliseconds timeout = DEFAULT_TIMEOUT);

    static LVSResult runKLayoutLVS(
        const LVSConfig& config,
        const std::string& workDir,
        const std::string& klayoutPath = "klayout",
        LogCallback onLog = nullptr,
        std::chrono::milliseconds timeout = DEFAULT_TIMEOUT);

    static DRCResult runKLayoutPEX(
        const PEXConfig& config,
        const std::string& workDir,
        const std::string& klayoutPath = "klayout",
        LogCallback onLog = nullptr,
        std::chrono::milliseconds timeout = DEFAULT_TIMEOUT);

    // ========================================================================
    // OPENROAD RCX (Parasitic Extraction)
    // ========================================================================
    static PnRResult runOpenROADRCX(
        const RCXConfig& config,
        const std::string& workDir,
        const std::string& openroadPath = "openroad",
        LogCallback onLog = nullptr,
        std::chrono::milliseconds timeout = DEFAULT_TIMEOUT);

    // ========================================================================
    // KLAYOUT GDSII EXPORT
    // ========================================================================
    static DRCResult runKLayoutGDSExport(
        const GDSExportConfig& config,
        const std::string& workDir,
        const std::string& klayoutPath = "klayout",
        LogCallback onLog = nullptr,
        std::chrono::milliseconds timeout = DEFAULT_TIMEOUT);
};

} // namespace acutesim::compute::orchestration
