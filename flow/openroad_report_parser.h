#pragma once

#include <string>
#include <vector>
#include "eda_types.h"

namespace acutesim::compute::orchestration {

// ============================================================================
// RESULT STRUCTS
// ============================================================================

struct PlacementResult {
    double totalArea = 0.0;
    double utilization = 0.0;
    int instanceCount = 0;
    int unplacedCount = 0;
};

struct TimingResult {
    double wns = 0.0;
    double tns = 0.0;
    int setupViolations = 0;
    int holdViolations = 0;
    bool timingClean = true;
};

struct RoutingResult {
    int routedNets = 0;
    int unroutedNets = 0;
    int drcViolations = 0;
    int antennaViolations = 0;
    bool drcClean = true;
};

struct PowerResult {
    double totalPower = 0.0;
    double dynamicPower = 0.0;
    double leakagePower = 0.0;
    double internalPower = 0.0;
};

struct PnRResult {
    bool success = false;

    PlacementResult placement;
    TimingResult    timing;
    RoutingResult   routing;
    PowerResult     power;

    std::vector<std::string> warnings;
    std::vector<std::string> errors;

    bool timingClean() const { return timing.timingClean; }
    bool drcClean() const    { return routing.drcClean; }

    std::string rawOutput;
};

// ============================================================================
// DEF COORDINATE TRANSLATION
// ============================================================================

/**
 * @brief Schematic component placement for DEF export.
 */
struct SchematicPlacement {
    std::string instanceName;
    std::string cellName;
    Point2D canvasPosition;       // Canvas coordinates (pixels, upper-left origin)
    double rotation = 0.0;        // Degrees: 0, 90, 180, 270
    bool mirrored = false;        // Y-axis mirror (FN orientation in DEF)
};

// ============================================================================
// PARSERS
// ============================================================================

class OpenRoadReportParser {
public:
    static PnRResult parse(const std::string& output);
    static void parseErrors(const std::string& stderrOutput, PnRResult& result);
    static PlacementResult parseDesignArea(const std::string& output);
    static TimingResult    parseTiming(const std::string& output);
    static PowerResult     parsePower(const std::string& output);
    static RoutingResult   parseRouting(const std::string& output);
};

class DEFTranslator {
public:
    static std::string generateDEF(
        const std::string& designName,
        const std::vector<SchematicPlacement>& placements,
        const Size2D& dieArea,
        double pixelToMicronScale = 0.1,
        double canvasHeight = 1000.0,
        int defUnitsPerMicron = 1000);

    static std::string toDefOrientation(double rotationDegrees, bool mirrored);

private:
    static Point2D canvasToDef(const Point2D& canvasPos,
                                double canvasHeight,
                                double pixelToMicronScale,
                                int defUnitsPerMicron);
};

} // namespace acutesim::compute::orchestration
