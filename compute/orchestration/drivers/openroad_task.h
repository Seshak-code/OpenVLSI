#pragma once

#include <QString>
#include <QStringList>
#include <QSizeF>

namespace acutesim::compute::orchestration {

/**
 * Optimization target for placement.
 */
enum class PlacementGoal {
    TIMING,         // Optimize for timing closure
    CONGESTION,     // Minimize routing congestion
    POWER,          // Optimize for power
    BALANCED        // Balance between all metrics
};

/**
 * Routing layer configuration.
 */
enum class RoutingEffort {
    LOW,            // Fast routing, may have DRC violations
    MEDIUM,         // Balanced effort
    HIGH            // High effort, clean DRC
};

/**
 * Configuration for floorplanning.
 */
struct FloorplanConfig {
    QString designName;
    QSizeF dieArea;                     // Die dimensions in micrometers
    double coreUtilization = 0.5;       // Target core utilization (0.0-1.0)
    double aspectRatio = 1.0;           // Width/Height ratio
    QString techLef;                    // Technology LEF file
    QStringList cellLefs;               // Cell library LEF files
    QString synthesizedNetlist;         // Input netlist (Verilog from Yosys)
    bool autoPlaceIO = true;            // Auto-place I/O pins
};

/**
 * Configuration for placement.
 */
struct PlacementConfig {
    QString designName;
    PlacementGoal goal = PlacementGoal::BALANCED;
    double targetDensity = 0.6;         // Placement density target
    bool globalPlacementOnly = false;   // Skip detailed placement
    int maxIterations = 0;              // 0 = tool default
};

/**
 * Configuration for clock tree synthesis.
 */
struct CTSConfig {
    QString clockNet;                   // Primary clock net name
    double targetSkew = 0.0;            // Target clock skew (ps)
    double maxCapacitance = 0.0;        // Max wire capacitance
    QString bufferCell;                 // Buffer cell for CTS
};

/**
 * Configuration for routing.
 */
struct RoutingConfig {
    QString designName;
    RoutingEffort effort = RoutingEffort::MEDIUM;
    bool globalRoutingOnly = false;     // Skip detailed routing
    int maxRoutingLayer = -1;           // -1 = use all available
    bool repairAntennas = true;         // Fix antenna violations
};

/**
 * @brief Generates OpenROAD TCL scripts for physical design flow.
 *
 * OpenROAD handles:
 * - Floorplanning: Die/core area definition, I/O placement
 * - Placement: Global and detailed standard cell placement
 * - CTS: Clock tree synthesis
 * - Routing: Global and detailed routing
 *
 * Following the Configuration-to-Script pattern, this class only generates
 * TCL scripts. Execution is handled by the orchestration layer.
 */
class OpenRoadTask {
public:
    /**
     * Generate floorplan initialization script.
     * Sets up die area, core utilization, and I/O placement.
     */
    static QString generateFloorplanTcl(const FloorplanConfig& config);

    /**
     * Generate placement script.
     * Performs global and detailed placement.
     */
    static QString generatePlacementTcl(const PlacementConfig& config);

    /**
     * Generate clock tree synthesis script.
     */
    static QString generateCTSTcl(const CTSConfig& config);

    /**
     * Generate routing script.
     * Performs global and detailed routing.
     */
    static QString generateRoutingTcl(const RoutingConfig& config);

    /**
     * Generate complete P&R flow script.
     * Combines floorplan, placement, CTS, and routing.
     */
    static QString generateFullFlowTcl(const FloorplanConfig& fpConfig,
                                        const PlacementConfig& placeConfig,
                                        const CTSConfig& ctsConfig,
                                        const RoutingConfig& routeConfig);

private:
    static QString getPlacementGoalOptions(PlacementGoal goal);
    static QString getRoutingEffortOptions(RoutingEffort effort);
};

} // namespace acutesim::compute::orchestration
