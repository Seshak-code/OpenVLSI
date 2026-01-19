#pragma once

#include "../components/visual_component.h"
#include <QGraphicsScene>
#include <QVector>
#include <QPoint>
#include <QString>
#include <QMap>
#include <set>

/**
 * erc_engine.h
 * 
 * Real-Time Electrical Rule Checking Engine
 * 
 * Features:
 * - Floating node/gate detection
 * - Short circuit detection
 * - Near-miss connection warnings
 * - Visual violation markers
 * - Simulation blocking on errors
 */

namespace ERC {

// ============================================================================
// VIOLATION TYPES
// ============================================================================

enum class ViolationType {
    NO_GROUND,           // Circuit has no ground reference
    FLOATING_NODE,       // Node with only one connection
    FLOATING_GATE,       // MOSFET gate without DC path
    SHORT_CIRCUIT,       // VDD to GND short (low resistance)
    MULTIPLE_DRIVERS,    // Multiple voltage sources on same net
    NEAR_MISS,           // Terminal almost connected (within 1-2 grid)
    OPEN_DRAIN           // Output without load path
};

enum class Severity {
    INFO,       // Advisory only
    WARNING,    // Should fix, but can simulate
    ERROR       // Must fix, blocks simulation
};

inline Severity getSeverity(ViolationType type) {
    switch (type) {
        case ViolationType::NO_GROUND:
        case ViolationType::SHORT_CIRCUIT:
        case ViolationType::FLOATING_GATE:
            return Severity::ERROR;
        case ViolationType::FLOATING_NODE:
        case ViolationType::MULTIPLE_DRIVERS:
        case ViolationType::OPEN_DRAIN:
            return Severity::WARNING;
        case ViolationType::NEAR_MISS:
            return Severity::INFO;
        default:
            return Severity::WARNING;
    }
}

inline QString typeToString(ViolationType type) {
    switch (type) {
        case ViolationType::NO_GROUND: return "No Ground";
        case ViolationType::FLOATING_NODE: return "Floating Node";
        case ViolationType::FLOATING_GATE: return "Floating Gate";
        case ViolationType::SHORT_CIRCUIT: return "Short Circuit";
        case ViolationType::MULTIPLE_DRIVERS: return "Multiple Drivers";
        case ViolationType::NEAR_MISS: return "Near Miss";
        case ViolationType::OPEN_DRAIN: return "Open Drain";
        default: return "Unknown";
    }
}

// ============================================================================
// VIOLATION STRUCT
// ============================================================================

struct Violation {
    ViolationType type;
    Severity severity;
    QString message;
    QPoint location;                    // Grid coordinates
    QList<QGraphicsItem*> affectedItems;
    
    // For auto-fix suggestions
    QPoint suggestedTarget;             // Where to connect
    bool hasAutoFix = false;
    
    Violation(ViolationType t, const QString& msg, QPoint loc = QPoint())
        : type(t), severity(getSeverity(t)), message(msg), location(loc) {}
};

// ============================================================================
// ERC ENGINE
// ============================================================================

class ERCEngine {
public:
    ERCEngine() = default;
    
    /**
     * Run full ERC analysis on the scene
     * Returns list of all violations found
     */
    QList<Violation> runFullCheck(QGraphicsScene* scene) {
        violations_.clear();
        
        // Build connectivity map
        buildConnectivityMap(scene);
        
        // Run individual checks
        checkGroundExists(scene);
        checkFloatingNodes(scene);
        checkFloatingGates(scene);
        checkShortCircuits(scene);
        checkNearMisses(scene);
        
        return violations_;
    }
    
    /**
     * Check if there are any blocking errors
     */
    bool hasBlockingErrors() const {
        for (const auto& v : violations_) {
            if (v.severity == Severity::ERROR) return true;
        }
        return false;
    }
    
    /**
     * Check if simulation is allowed
     */
    bool canSimulate() const {
        return !hasBlockingErrors();
    }
    
    /**
     * Get error count
     */
    int errorCount() const {
        int count = 0;
        for (const auto& v : violations_) {
            if (v.severity == Severity::ERROR) count++;
        }
        return count;
    }
    
    /**
     * Get warning count
     */
    int warningCount() const {
        int count = 0;
        for (const auto& v : violations_) {
            if (v.severity == Severity::WARNING) count++;
        }
        return count;
    }
    
    /**
     * Get all violations
     */
    const QList<Violation>& violations() const { return violations_; }
    
    /**
     * Get summary string for status bar
     */
    QString getSummary() const {
        int errors = errorCount();
        int warnings = warningCount();
        if (errors == 0 && warnings == 0) {
            return "ERC: ✅ OK";
        } else if (errors > 0) {
            return QString("ERC: ❌ %1 error(s), %2 warning(s)").arg(errors).arg(warnings);
        } else {
            return QString("ERC: ⚠️ %1 warning(s)").arg(warnings);
        }
    }

private:
    QList<Violation> violations_;
    
    // Connectivity tracking
    QMap<QPair<int,int>, int> pointToNetID_;
    QMap<int, int> netConnectionCount_;
    QMap<int, bool> netHasGround_;
    QMap<int, bool> netHasVoltageSource_;
    bool hasGroundComponent_ = false;
    
    void buildConnectivityMap(QGraphicsScene* scene) {
        pointToNetID_.clear();
        netConnectionCount_.clear();
        netHasGround_.clear();
        netHasVoltageSource_.clear();
        hasGroundComponent_ = false;
        
        int nextNetID = 1;
        
        auto getNetID = [&](int x, int y) -> int {
            QPair<int,int> key = {x, y};
            if (!pointToNetID_.contains(key)) {
                pointToNetID_[key] = nextNetID++;
            }
            return pointToNetID_[key];
        };
        
        // Scan all wires
        for (QGraphicsItem* item : scene->items()) {
            if (auto* wire = dynamic_cast<VisualWire*>(item)) {
                QLineF line = wire->line();
                QPointF p1 = wire->mapToScene(line.p1());
                QPointF p2 = wire->mapToScene(line.p2());
                
                int x1 = qRound(p1.x()), y1 = qRound(p1.y());
                int x2 = qRound(p2.x()), y2 = qRound(p2.y());
                
                int netID = getNetID(x1, y1);
                
                // Walk the wire and unify all points
                if (x1 == x2) { // Vertical
                    int minY = std::min(y1, y2);
                    int maxY = std::max(y1, y2);
                    for (int y = minY; y <= maxY; y += GRID_STEP) {
                        pointToNetID_[{x1, y}] = netID;
                        netConnectionCount_[netID]++;
                    }
                } else if (y1 == y2) { // Horizontal
                    int minX = std::min(x1, x2);
                    int maxX = std::max(x1, x2);
                    for (int x = minX; x <= maxX; x += GRID_STEP) {
                        pointToNetID_[{x, y1}] = netID;
                        netConnectionCount_[netID]++;
                    }
                }
            }
        }
        
        // Scan components for terminals
        for (QGraphicsItem* item : scene->items()) {
            if (auto* comp = dynamic_cast<VisualComponent*>(item)) {
                ComponentType type = comp->type;
                
                if (type == ComponentType::Ground) {
                    hasGroundComponent_ = true;
                }
                
                QVector<QPointF> terms = comp->getTerminals();
                for (const QPointF& localP : terms) {
                    QPointF sceneP = comp->mapToScene(localP);
                    int x = qRound(sceneP.x()), y = qRound(sceneP.y());
                    int netID = getNetID(x, y);
                    netConnectionCount_[netID]++;
                    
                    if (type == ComponentType::Ground) {
                        netHasGround_[netID] = true;
                    }
                    if (type == ComponentType::VoltageSource) {
                        netHasVoltageSource_[netID] = true;
                    }
                }
            }
        }
    }
    
    void checkGroundExists(QGraphicsScene* scene) {
        Q_UNUSED(scene);
        if (!hasGroundComponent_) {
            violations_.append(Violation(
                ViolationType::NO_GROUND,
                "No ground reference found. Add a Ground component.",
                QPoint(0, 0)
            ));
        }
    }
    
    void checkFloatingNodes(QGraphicsScene* scene) {
        Q_UNUSED(scene);
        for (auto it = netConnectionCount_.begin(); it != netConnectionCount_.end(); ++it) {
            if (it.value() <= 1) {
                // Find a point on this net
                for (auto pit = pointToNetID_.begin(); pit != pointToNetID_.end(); ++pit) {
                    if (pit.value() == it.key()) {
                        violations_.append(Violation(
                            ViolationType::FLOATING_NODE,
                            QString("Net at (%1, %2) has only 1 connection")
                                .arg(pit.key().first).arg(pit.key().second),
                            QPoint(pit.key().first, pit.key().second)
                        ));
                        break;
                    }
                }
            }
        }
    }
    
    void checkFloatingGates(QGraphicsScene* scene) {
        for (QGraphicsItem* item : scene->items()) {
            if (auto* comp = dynamic_cast<VisualComponent*>(item)) {
                if (comp->type == ComponentType::Mosfet) {
                    // Gate is terminal 0 for MOSFETs
                    QVector<QPointF> terms = comp->getTerminals();
                    if (terms.size() >= 1) {
                        QPointF gatePos = comp->mapToScene(terms[0]);
                        int gx = qRound(gatePos.x()), gy = qRound(gatePos.y());
                        QPair<int,int> gateKey = {gx, gy};
                        
                        if (pointToNetID_.contains(gateKey)) {
                            int netID = pointToNetID_[gateKey];
                            // Gate needs DC path (resistor or another gate)
                            // For now, just check if it has more than 1 connection
                            if (netConnectionCount_[netID] <= 1) {
                                violations_.append(Violation(
                                    ViolationType::FLOATING_GATE,
                                    QString("MOSFET %1 has floating gate (no DC path)")
                                        .arg(comp->name),
                                    QPoint(gx, gy)
                                ));
                            }
                        } else {
                            // Gate not connected at all!
                            violations_.append(Violation(
                                ViolationType::FLOATING_GATE,
                                QString("MOSFET %1 gate is not connected")
                                    .arg(comp->name),
                                QPoint(gx, gy)
                            ));
                        }
                    }
                }
            }
        }
    }
    
    void checkShortCircuits(QGraphicsScene* scene) {
        // Check for VDD connected directly to GND (or through very low resistance)
        // Simple heuristic: if a net has both ground and voltage source
        for (auto it = netHasGround_.begin(); it != netHasGround_.end(); ++it) {
            int netID = it.key();
            if (netHasVoltageSource_.value(netID, false)) {
                // This net has both ground and voltage source!
                for (auto pit = pointToNetID_.begin(); pit != pointToNetID_.end(); ++pit) {
                    if (pit.value() == netID) {
                        violations_.append(Violation(
                            ViolationType::SHORT_CIRCUIT,
                            "Voltage source shorted to ground!",
                            QPoint(pit.key().first, pit.key().second)
                        ));
                        break;
                    }
                }
            }
        }
    }
    
    void checkNearMisses(QGraphicsScene* scene) {
        // Find terminals that are 1 grid step apart but not connected
        QList<QPair<int,int>> terminalPoints;
        
        for (QGraphicsItem* item : scene->items()) {
            if (auto* comp = dynamic_cast<VisualComponent*>(item)) {
                for (const QPointF& localP : comp->getTerminals()) {
                    QPointF sceneP = comp->mapToScene(localP);
                    terminalPoints.append({qRound(sceneP.x()), qRound(sceneP.y())});
                }
            }
        }
        
        // O(n²) but terminals are few
        std::set<QPair<QPair<int,int>, QPair<int,int>>> reported;
        for (int i = 0; i < terminalPoints.size(); ++i) {
            for (int j = i + 1; j < terminalPoints.size(); ++j) {
                auto p1 = terminalPoints[i];
                auto p2 = terminalPoints[j];
                
                int dx = std::abs(p1.first - p2.first);
                int dy = std::abs(p1.second - p2.second);
                
                // Near miss: exactly 1 grid step away (not connected)
                if ((dx == GRID_STEP && dy == 0) || (dx == 0 && dy == GRID_STEP)) {
                    // Check if they're on the same net
                    int net1 = pointToNetID_.value(p1, -1);
                    int net2 = pointToNetID_.value(p2, -1);
                    
                    if (net1 != net2 || net1 == -1) {
                        // Not connected - this is a near miss!
                        auto key = std::make_pair(p1, p2);
                        if (reported.find(key) == reported.end()) {
                            reported.insert(key);
                            
                            Violation v(
                                ViolationType::NEAR_MISS,
                                QString("Terminals at (%1,%2) and (%3,%4) are 1 grid apart but not connected")
                                    .arg(p1.first).arg(p1.second).arg(p2.first).arg(p2.second),
                                QPoint(p1.first, p1.second)
                            );
                            v.suggestedTarget = QPoint(p2.first, p2.second);
                            v.hasAutoFix = true;
                            violations_.append(v);
                        }
                    }
                }
            }
        }
    }
};

} // namespace ERC
