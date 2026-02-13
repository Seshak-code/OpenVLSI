#pragma once

#include "../../components/circuit.h"
#include "../editors/schematic/schematic_editor.h"

namespace acutesim {
namespace gui {
namespace components {

/**
 * @brief Helper to generate visual components from a raw TensorBlock.
 * Used for drill-down into code-defined Standard Cells.
 */
class StandardCellVisualizer {
public:
    /**
     * @brief Populates the given scene with visual components representing the internal logic of the block.
     * @param scene The scene to populate (should be cleared first).
     * @param block The internal TensorBlock of the standard cell.
     * @param cellName The name of the cell for display purposes.
     */
    static void populateScene(GridScene* scene, const TensorBlock& block, const QString& cellName);
};

} // namespace components
} // namespace gui
} // namespace acutesim
