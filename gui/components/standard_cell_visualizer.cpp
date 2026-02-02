#include "standard_cell_visualizer.h"
#include <QJsonObject>
#include <cmath>

namespace acutesim {
namespace gui {
namespace components {

void StandardCellVisualizer::populateScene(GridScene* scene, const TensorBlock& block, const QString& cellName) {
    // 1. Clear existing items (safety)
    scene->clear();
    
    // 2. Setup Layout Variables
    double x = 0;
    double y = 0;
    double spacing = 150.0;
    int cols = 4;
    int index = 0;
    
    // Helper to add component
    auto addVisual = [&](const std::string& type, const std::string& name, const std::string& label) {
        QJsonObject params;
        params["label"] = QString::fromStdString(label);
        
        // Calculate Grid Position
        double curX = (index % cols) * spacing;
        double curY = (index / cols) * spacing;
        
        scene->placeComponent(QString::fromStdString(type), QPointF(curX, curY), 0, params);
        index++;
    };
    
    // 3. Iterate over devices and place them
    // Note: We need access to the internal lists of TensorBlock. 
    // Assuming TensorBlock exposes devices via public vectors or we iterate known types.
    
    // Resistors
    for (const auto& r : block.resistors) {
        addVisual("Resistor", "R", "Resistor " + std::to_string(r.nodeTerminal1) + "-" + std::to_string(r.nodeTerminal2));
    }
    
    // Capacitors
    for (const auto& c : block.capacitors) {
        addVisual("Capacitor", "C", "Cap " + std::to_string(c.nodePlate1) + "-" + std::to_string(c.nodePlate2));
    }
    
    // MOSFETs (Unified Vector)
    for (const auto& m : block.mosfets) {
        std::string visualType = "PDK_NFET"; // Default
        std::string label = "NFET";
        
        // Simple heuristic for type
        if (m.modelName.find("pfet") != std::string::npos || m.modelName.find("pmos") != std::string::npos) {
            visualType = "PDK_PFET";
            label = "PFET";
        }
        
        addVisual(visualType, "M", label);
    }
    
    // BJTs (NPN)
    for (const auto& q : block.bjts) {
        // Simple mapping, assuming NPN default
        addVisual("NPN", "Q", "BJT");
    }
    
    // 4. Add Input/Output Pins (Ports)
    // TensorBlock typically abstracts ports as node indices. 
    // Ideally, we would list external pins here if we had the mapping.
    // For now, we just list the components found.
    
    scene->triggerTopologyUpdate();
}

} // namespace components
} // namespace gui
} // namespace acutesim
