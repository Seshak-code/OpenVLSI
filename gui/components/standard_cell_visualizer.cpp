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

void StandardCellVisualizer::populateScene(GridScene* scene, const TensorBlock& block, const QString& cellName) {
    // 1. Clear existing items
    scene->clear();
    
    // 2. Node-to-Terminal Map
    std::map<int, QVector<TerminalRef>> nodeToTerminals;
    
    // 3. Layout Variables
    double spacing = 150.0;
    int cols = 4;
    int index = 0;
    
    // Helper: Place component and register terminals
    auto placeAndTrack = [&](const QString& type, const std::vector<int>& nodes, const QString& label = "") {
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
        
        // Register terminals to node map
        auto terminals = comp->getTerminals();
        for (int i = 0; i < std::min(static_cast<int>(nodes.size()), static_cast<int>(terminals.size())); ++i) {
            int nodeIdx = nodes[i];
            nodeToTerminals[nodeIdx].append({comp, i});
        }
    };
    
    // =========================================================================
    // 4. REGISTER EXTERNAL PINS FIRST
    // =========================================================================
    for (size_t i = 0; i < block.pinNames.size(); ++i) {
        int internalNode = (i < block.portNodeIndices.size()) 
            ? block.portNodeIndices[i] 
            : static_cast<int>(i + 1);
        
        QString pinName = QString::fromStdString(block.pinNames[i]);
        placeAndTrack("Pin", {internalNode}, pinName);
    }
    
    // =========================================================================
    // 5. PLACE INTERNAL COMPONENTS
    // =========================================================================
    
    // Resistors: 2 terminals (n1, n2)
    for (const auto& r : block.resistors) {
        placeAndTrack("Resistor", {r.nodeTerminal1, r.nodeTerminal2}, 
                      QString::fromStdString(r.name));
    }
    
    // Capacitors: 2 terminals (plate1, plate2)
    for (const auto& c : block.capacitors) {
        placeAndTrack("Capacitor", {c.nodePlate1, c.nodePlate2},
                      QString::fromStdString(c.name));
    }
    
    // MOSFETs: 4 terminals (D, G, S, B)
    for (const auto& m : block.mosfets) {
        QString visualType = "PDK_NFET";
        if (m.modelName.find("pfet") != std::string::npos || 
            m.modelName.find("pmos") != std::string::npos) {
            visualType = "PDK_PFET";
        }
        // Terminal ordering: D=0, G=1, S=2, B=3
        placeAndTrack(visualType, {m.drain, m.gate, m.source, m.body},
                      QString::fromStdString(m.instanceName));
    }
    
    // BJTs: 3 terminals (C, B, E)
    for (const auto& q : block.bjts) {
        QString visualType = q.isNPN ? "NPN" : "PNP";
        // Terminal ordering: C=0, B=1, E=2
        placeAndTrack(visualType, {q.nodeCollector, q.base, q.emitter},
                      QString::fromStdString(q.instanceName));
    }
    
    // Diodes: 2 terminals (anode, cathode)
    for (const auto& d : block.diodes) {
        placeAndTrack("Diode", {d.anode, d.cathode});
    }
    
    // =========================================================================
    // 6. CREATE WIRES (Junction-Based for 3+ terminals)
    // =========================================================================
    for (const auto& [nodeIdx, termList] : nodeToTerminals) {
        if (nodeIdx == 0) continue; // Skip ground node (handled separately)
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
