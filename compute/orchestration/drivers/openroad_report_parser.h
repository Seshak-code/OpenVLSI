#pragma once

#include <QString>
#include <QStringList>
#include <QPointF>
#include <QSizeF>

namespace acutesim::compute::orchestration {

// ============================================================================
// RESULT STRUCTS
// ============================================================================

struct PlacementResult {
    double totalArea = 0.0;          // Total design area (um²)
    double utilization = 0.0;        // Core utilization (0.0 - 1.0)
    int instanceCount = 0;           // Number of placed instances
    int unplacedCount = 0;           // Number of unplaced instances
};

struct TimingResult {
    double wns = 0.0;               // Worst Negative Slack (ps)
    double tns = 0.0;               // Total Negative Slack (ps)
    int setupViolations = 0;        // Number of setup violations
    int holdViolations = 0;         // Number of hold violations
    bool timingClean = true;        // Derived: true if setupViolations + holdViolations == 0
};

struct RoutingResult {
    int routedNets = 0;             // Number of successfully routed nets
    int unroutedNets = 0;           // Number of unrouted nets
    int drcViolations = 0;          // DRC violations
    int antennaViolations = 0;      // Antenna violations
    bool drcClean = true;           // Derived: true if drcViolations == 0
};

struct PowerResult {
    double totalPower = 0.0;        // Total power (mW)
    double dynamicPower = 0.0;      // Dynamic/switching power (mW)
    double leakagePower = 0.0;      // Static/leakage power (mW)
    double internalPower = 0.0;     // Internal cell power (mW)
};

struct PnRResult {
    bool success = false;

    PlacementResult placement;
    TimingResult timing;
    RoutingResult routing;
    PowerResult power;

    QStringList warnings;
    QStringList errors;

    // Quick-check flags (derived from sub-results)
    bool timingClean() const { return timing.timingClean; }
    bool drcClean() const { return routing.drcClean; }

    QString rawOutput;
};

// ============================================================================
// DEF COORDINATE TRANSLATION
// ============================================================================

/**
 * @brief Schematic component placement for DEF export.
 *
 * Represents a single standard cell instance positioned in canvas space.
 * DEF uses lower-left origin (Y increases upward) while most schematic
 * canvases use upper-left origin (Y increases downward). The DEF translator
 * handles this coordinate inversion automatically.
 */
struct SchematicPlacement {
    QString instanceName;           // e.g. "U1"
    QString cellName;               // e.g. "sky130_fd_sc_hd__inv_1"
    QPointF canvasPosition;         // Canvas coordinates (pixels, upper-left origin)
    double rotation = 0.0;          // Degrees: 0, 90, 180, 270
    bool mirrored = false;          // Y-axis mirror (FN orientation in DEF)
};

// ============================================================================
// PARSERS
// ============================================================================

/**
 * @brief Parses OpenROAD stdout to extract structured P&R results.
 *
 * Handles the standard OpenROAD report formats:
 *   - report_design_area → total area, utilization
 *   - report_worst_slack -max → WNS/TNS, violations
 *   - report_power → power breakdown
 *   - check_routing → DRC + antenna violations
 */
class OpenRoadReportParser {
public:
    /**
     * Parse the complete stdout output from an OpenROAD run.
     */
    static PnRResult parse(const QString& output);

    /**
     * Parse error/warning stream from stderr.
     */
    static void parseErrors(const QString& stderr, PnRResult& result);

    /**
     * Parse only the design area report section.
     */
    static PlacementResult parseDesignArea(const QString& output);

    /**
     * Parse only the timing report section.
     */
    static TimingResult parseTiming(const QString& output);

    /**
     * Parse only the power report section.
     */
    static PowerResult parsePower(const QString& output);

    /**
     * Parse routing check output.
     */
    static RoutingResult parseRouting(const QString& output);
};

/**
 * @brief Translates schematic canvas positions to DEF placement format.
 *
 * DEF coordinate system:
 *   - Origin: lower-left corner of die
 *   - Units: database units (typically micrometers × DEF_UNITS_PER_MICRON)
 *   - Y-axis: increases upward (opposite of canvas convention)
 *
 * Canvas coordinate system:
 *   - Origin: upper-left corner
 *   - Units: pixels
 *   - Y-axis: increases downward
 */
class DEFTranslator {
public:
    /**
     * Generate a complete DEF file from schematic placements.
     *
     * @param designName   Top-level design name
     * @param placements   List of component placements in canvas coordinates
     * @param dieArea      Die dimensions in micrometers
     * @param pixelToMicronScale  Conversion factor: 1 pixel = N micrometers
     * @param canvasHeight Canvas height in pixels (needed for Y-axis flip)
     * @param defUnitsPerMicron  DEF coordinate precision (default: 1000)
     */
    static QString generateDEF(
        const QString& designName,
        const QList<SchematicPlacement>& placements,
        const QSizeF& dieArea,
        double pixelToMicronScale = 0.1,
        double canvasHeight = 1000.0,
        int defUnitsPerMicron = 1000);

    /**
     * Convert a DEF orientation string from rotation + mirror.
     * @return DEF orientation: "N", "S", "E", "W", "FN", "FS", "FE", "FW"
     */
    static QString toDefOrientation(double rotationDegrees, bool mirrored);

private:
    /**
     * Convert canvas (upper-left origin) to DEF (lower-left origin).
     * Flips Y-axis: defY = (canvasHeight - canvasY) * scale
     */
    static QPointF canvasToDef(const QPointF& canvasPos,
                                double canvasHeight,
                                double pixelToMicronScale,
                                int defUnitsPerMicron);
};

} // namespace acutesim::compute::orchestration
