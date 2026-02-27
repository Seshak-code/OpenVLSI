#include "standard_cell_visualizer.h"
#include <QJsonObject>
#include <cmath>
#include <map>
#include <numeric>

namespace acutesim {
namespace gui {
namespace components {

// ============================================================================
// TERMINAL REFERENCE (Survives re-layout)
// ============================================================================
struct TerminalRef {
    VisualComponent* comp;
    int terminalIndex;
    
    QPointF scenePos() const {
        if (!comp) return QPointF();
        auto terminals = comp->getTerminals();
        if (terminalIndex < 0 || terminalIndex >= terminals.size()) return QPointF();
        return comp->mapToScene(terminals[terminalIndex]);
    }
};

// Helper: Compute centroid of terminal positions
static QPointF centroid(const QVector<TerminalRef>& refs) {
    if (refs.isEmpty()) return QPointF();
    double sumX = 0, sumY = 0;
    for (const auto& ref : refs) {
        QPointF p = ref.scenePos();
        sumX += p.x();
        sumY += p.y();
    }
    return QPointF(sumX / refs.size(), sumY / refs.size());
}

void StandardCellVisualizer::populateScene(GridScene* scene, const acutesim::compute::orchestration::SchematicGraphDTO& graph, const QString& cellName) {
    // 1. Clear existing items
    scene->clear();
    
    // 2. Net-to-Terminal Map
    std::map<std::string, QVector<TerminalRef>> netToTerminals;
    
    // 3. Layout Variables
    double spacing = 150.0;
    int cols = 4;
    int index = 0;
    
    // Helper: Place component and register terminals
    auto placeAndTrack = [&](const QString& type, const std::vector<std::string>& nets, const QString& label = "") {
        // Calculate Grid Position
        double curX = (index % cols) * spacing;
        double curY = (index / cols) * spacing;
        
        QJsonObject params;
        if (!label.isEmpty()) {
            params["label"] = label;
        }
        
        QString compId = scene->placeComponent(type, QPointF(curX, curY), 0, params);
        index++;
        
        // Retrieve placed component
        VisualComponent* comp = scene->findComponent(compId);
        if (!comp) return;
        
        // Register terminals to net map
        auto terminals = comp->getTerminals();
        for (int i = 0; i < std::min(static_cast<int>(nets.size()), static_cast<int>(terminals.size())); ++i) {
            std::string netName = nets[i];
            netToTerminals[netName].append({comp, i});
        }
    };
    
    // =========================================================================
    // 4. REGISTER INTERFACE PORTS FIRST
    // =========================================================================
    for (const auto& portName : graph.portNames) {
        placeAndTrack("Pin", {portName}, QString::fromStdString(portName));
    }
    
    // =========================================================================
    // 5. PLACE INSTANCES
    // =========================================================================
    for (const auto& inst : graph.instances) {
        QString visualType = "Generic";
        if (inst.type == "Resistor") visualType = "Resistor";
        else if (inst.type == "Capacitor") visualType = "Capacitor";
        else if (inst.type == "Mosfet") {
             auto itModel = inst.parameters.find("model");
             if (itModel != inst.parameters.end() && (itModel->second.find("pfet") != std::string::npos || itModel->second.find("pmos") != std::string::npos)) {
                 visualType = "PDK_PFET";
             } else {
                 visualType = "PDK_NFET";
             }
        } else if (inst.type == "BJT") {
             auto itNPN = inst.parameters.find("isNPN");
             bool isNPN = itNPN != inst.parameters.end() && itNPN->second == "1";
             visualType = isNPN ? "NPN" : "PNP";
        } else if (inst.type == "Diode") visualType = "Diode";

        placeAndTrack(visualType, inst.connectedNets, QString::fromStdString(inst.instanceName));
    }
    
    // =========================================================================
    // 6. CREATE WIRES (Junction-Based for 3+ terminals)
    // =========================================================================
    for (const auto& [netName, termList] : netToTerminals) {
        if (netName == "0" || netName == "GND" || netName == "gnd") continue; 
        if (termList.size() < 2) continue;
        
        if (termList.size() == 2) {
            // Direct connection
            QPointF p1 = termList[0].scenePos();
            QPointF p2 = termList[1].scenePos();
            scene->placeWirePath({p1, p2});
        } else {
            // Junction-based: connect all to centroid
            QPointF junction = centroid(termList);
            // Snap junction to grid
            junction = scene->snapToGrid(junction);
            
            for (const auto& ref : termList) {
                QPointF termPos = ref.scenePos();
                scene->placeWirePath({termPos, junction});
            }
        }
    }
    
    // 7. Trigger topology update
    scene->triggerTopologyUpdate();
}

} // namespace components
} // namespace gui
} // namespace acutesim
