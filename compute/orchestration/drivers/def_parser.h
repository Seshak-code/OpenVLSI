#pragma once

#include <string>
#include <vector>
#include "eda_types.h"

namespace acutesim::compute::orchestration {

/**
 * @brief A single placed component from a DEF COMPONENTS section.
 */
struct DEFPlacement {
    std::string instanceName;
    std::string cellName;
    Point2D defPosition;    // In DEF database units
    std::string orientation; // N, S, E, W, FN, FS, FE, FW
};

/**
 * @brief Parsed DEF design data (subset: header + placements).
 */
struct DEFDesign {
    std::string designName;
    Rect2D dieArea;                   // In DEF database units
    int unitsPerMicron = 1000;
    std::vector<DEFPlacement> placements;
};

/**
 * @brief Lightweight DEF parser for extracting placement data.
 *
 * Parses: VERSION, DESIGN, UNITS, DIEAREA, COMPONENTS section.
 */
class DEFParser {
public:
    static DEFDesign parse(const std::string& defContent);

    /**
     * Convert DEF coordinates back to canvas coordinates.
     */
    static Point2D defToCanvas(const Point2D& defPos,
                                double canvasHeight,
                                double pixelToMicronScale,
                                int defUnitsPerMicron);
};

} // namespace acutesim::compute::orchestration
