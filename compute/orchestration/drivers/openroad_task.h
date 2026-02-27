#pragma once

#include <string>
#include <vector>
#include "eda_types.h"

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
    std::string designName;
    Size2D dieArea;                         // Die dimensions in micrometers
    double coreUtilization = 0.5;           // Target core utilization (0.0-1.0)
    double aspectRatio = 1.0;               // Width/Height ratio
    std::string techLef;                    // Technology LEF file
    std::vector<std::string> cellLefs;      // Cell library LEF files
    std::string synthesizedNetlist;         // Input netlist (Verilog from Yosys)
    bool autoPlaceIO = true;                // Auto-place I/O pins
};

/**
 * Configuration for placement.
 */
struct PlacementConfig {
    std::string designName;
    PlacementGoal goal = PlacementGoal::BALANCED;
    double targetDensity = 0.6;             // Placement density target
    bool globalPlacementOnly = false;       // Skip detailed placement
    int maxIterations = 0;                  // 0 = tool default
};

/**
 * Configuration for clock tree synthesis.
 */
struct CTSConfig {
    std::string clockNet;                   // Primary clock net name
    double targetSkew = 0.0;               // Target clock skew (ps)
    double maxCapacitance = 0.0;           // Max wire capacitance
    std::string bufferCell;                 // Buffer cell for CTS
};

/**
 * Configuration for parasitic extraction via OpenROAD RCX.
 */
struct RCXConfig {
    std::string designName;
    std::string placedRoutedDef;            // Input routed DEF file
    std::string techLef;                    // Technology LEF
    std::vector<std::string> cellLefs;      // Cell library LEFs
    std::string outputSpef;                 // Output SPEF file path
    std::string rcxRuleFile;                // RC extraction rule deck (.lef or .rcx)
    bool couplingCaps = true;               // Include coupling capacitances
    double capFactor  = 1.0;               // Capacitance calibration factor
    double resFactor  = 1.0;               // Resistance calibration factor
};

/**
 * Configuration for routing.
 */
struct RoutingConfig {
    std::string designName;
    RoutingEffort effort = RoutingEffort::MEDIUM;
    bool globalRoutingOnly = false;         // Skip detailed routing
    int maxRoutingLayer = -1;               // -1 = use all available
    bool repairAntennas = true;             // Fix antenna violations
};

/**
 * @brief Generates OpenROAD TCL scripts for physical design flow.
 *
 * Following the Configuration-to-Script pattern, this class only generates
 * TCL scripts. Execution is handled by the orchestration layer.
 */
class OpenRoadTask {
public:
    static std::string generateFloorplanTcl(const FloorplanConfig& config);
    static std::string generatePlacementTcl(const PlacementConfig& config);
    static std::string generateCTSTcl(const CTSConfig& config);
    static std::string generateRoutingTcl(const RoutingConfig& config);
    static std::string generateFullFlowTcl(const FloorplanConfig& fpConfig,
                                            const PlacementConfig& placeConfig,
                                            const CTSConfig& ctsConfig,
                                            const RoutingConfig& routeConfig);
    static std::string generateRCXTcl(const RCXConfig& config);

private:
    static std::string getPlacementGoalOptions(PlacementGoal goal);
    static std::string getRoutingEffortOptions(RoutingEffort effort);
};

} // namespace acutesim::compute::orchestration
