#pragma once

#include "../components/visual_component.h"
#include <QGraphicsScene>
#include <QVector>
#include <QPoint>
#include <QString>
#include <set>
#include "../core/gui_types.h"

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
 * - Mode-specific rules (Bench vs Block vs System)
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
    OPEN_DRAIN,          // Output without load path

    // Solver Violations (Dynamic)
    SOLVER_SINGULAR_MATRIX,  // LU decomposition failed (rank deficiency)
    SOLVER_DIVERGENCE,       // Newton-Raphson failed to converge
    SOLVER_TIMEOUT,          // Simulation exceeded iteration/time limit
    SOLVER_LIMIT_CYCLE,      // NR oscillating between states
    SOLVER_SLOW_CONVERGENCE, // NR converging but too slowly
    SOLVER_EXPONENTIAL_OVERFLOW, // Device exponential overflow (voltage step > 100V)

    // Power Domain Violations
    CROSS_DOMAIN_SIGNAL,     // Signal crosses power domains without level shifter

    // Hierarchy Violations
    UNDRIVEN_POWER_PORT      // PowerPort component not connected to any net
};

enum class Severity {
    INFO,       // Advisory only
    WARNING,    // Should fix, but can simulate
    ERR       // Must fix, blocks simulation
};

inline Severity getSeverity(ViolationType type) {
    switch (type) {
        case ViolationType::NO_GROUND:
        case ViolationType::SHORT_CIRCUIT:
        case ViolationType::FLOATING_GATE:
        case ViolationType::SOLVER_SINGULAR_MATRIX:
        case ViolationType::SOLVER_DIVERGENCE:
             return Severity::ERR;
        case ViolationType::FLOATING_NODE:
        case ViolationType::MULTIPLE_DRIVERS:
        case ViolationType::OPEN_DRAIN:
        case ViolationType::SOLVER_TIMEOUT:
        case ViolationType::SOLVER_LIMIT_CYCLE:
        case ViolationType::SOLVER_SLOW_CONVERGENCE:
        case ViolationType::SOLVER_EXPONENTIAL_OVERFLOW:
        case ViolationType::CROSS_DOMAIN_SIGNAL:
        case ViolationType::UNDRIVEN_POWER_PORT:
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
        case ViolationType::SOLVER_SINGULAR_MATRIX: return "Singular Matrix (Solver)";
        case ViolationType::SOLVER_DIVERGENCE: return "Divergence (Solver)";
        case ViolationType::SOLVER_TIMEOUT: return "Timeout (Solver)";
        case ViolationType::SOLVER_LIMIT_CYCLE: return "Limit Cycle (Solver)";
        case ViolationType::SOLVER_SLOW_CONVERGENCE: return "Slow Convergence (Solver)";
        case ViolationType::SOLVER_EXPONENTIAL_OVERFLOW: return "Exponential Overflow (Solver)";
        case ViolationType::CROSS_DOMAIN_SIGNAL: return "Cross-Domain Signal";
        case ViolationType::UNDRIVEN_POWER_PORT: return "Undriven Power Port";
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
     *
     * NOTE: Mode-specific checks have been removed (unified component library).
     * All standard electrical checks (floating nodes, shorts, etc.) still apply.
     */
    QList<Violation> runFullCheck(QGraphicsScene* scene) {
        violations_.clear();

        // Build connectivity map
        buildConnectivityMap(scene);

        // Universal electrical checks (always run regardless of mode)
        checkGroundExists(scene);  // Always check for ground reference
        checkFloatingNodes(scene);
        checkFloatingGates(scene);
        checkShortCircuits(scene);
        checkNearMisses(scene);
        checkMultipleDrivers(scene);
        checkCrossDomainSignals(scene); // Power domain isolation check
        checkPowerPorts(scene);         // Floating PowerPort detection

        return violations_;
    }
    
    /**
     * Check if there are any blocking errors
     */
    bool hasBlockingErrors() const {
        for (const auto& v : violations_) {
            if (v.severity == Severity::ERR) return true;
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
            if (v.severity == Severity::ERR) count++;
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
    /**
     * Get summary string for status bar
     */
    QString getSummary() const {
        int errors = errorCount();
        int warnings = warningCount();
        if (errors == 0 && warnings == 0) {
            return "ERC: OK";
        } else if (errors > 0) {
            return QString("ERC: %1 error(s), %2 warning(s)").arg(errors).arg(warnings);
        } else {
            return QString("ERC: %1 warning(s)").arg(warnings);
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
                
                if (type == ComponentType::Ground || type == ComponentType::PDK_VSS) {
                    hasGroundComponent_ = true;
                }
                
                QVector<QPointF> terms = comp->getTerminals();
                for (const QPointF& localP : terms) {
                    QPointF sceneP = comp->mapToScene(localP);
                    int x = qRound(sceneP.x()), y = qRound(sceneP.y());
                    int netID = getNetID(x, y);
                    netConnectionCount_[netID]++;
                    
                    if (type == ComponentType::Ground || type == ComponentType::PDK_VSS) {
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
        if (hasGroundComponent_) return;

        // Check for Pins named GND/VSS (for hierarchical blocks)
        bool hasGroundPin = false;
        for (QGraphicsItem* item : scene->items()) {
            if (auto* comp = dynamic_cast<VisualComponent*>(item)) {
                if (comp->type == ComponentType::Pin) {
                    QString name = comp->name.toUpper();
                    if (name == "GND" || name == "VSS" || name == "GROUND" || name == "0") {
                        hasGroundPin = true;
                        break;
                    }
                }
            }
        }

        if (!hasGroundPin) {
            violations_.append(Violation(
                ViolationType::NO_GROUND,
                "No ground reference found. Add a Ground component or a Pin named GND/VSS.",
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
        // 1. Check for Voltage Source Self-Shorts (Plus connected to Minus)
        for (QGraphicsItem* item : scene->items()) {
            if (auto* comp = dynamic_cast<VisualComponent*>(item)) {
                if (comp->type == ComponentType::VoltageSource || 
                    comp->type == ComponentType::VCVS || 
                    comp->type == ComponentType::CCVS) {
                    
                    QVector<QPointF> terms = comp->getTerminals();
                    if (terms.size() >= 2) {
                        QPointF p1 = comp->mapToScene(terms[0]);
                        QPointF p2 = comp->mapToScene(terms[1]);
                        
                        QPair<int,int> k1 = {qRound(p1.x()), qRound(p1.y())};
                        QPair<int,int> k2 = {qRound(p2.x()), qRound(p2.y())};
                        
                        int net1 = pointToNetID_.value(k1, -1);
                        int net2 = pointToNetID_.value(k2, -1);
                        
                        if (net1 != -1 && net1 == net2) {
                             violations_.append(Violation(
                                ViolationType::SHORT_CIRCUIT,
                                QString("Source %1 is shorted ( terminals connected to same net)").arg(comp->name),
                                k1.first == 0 ? QPoint(k2.first, k2.second) : QPoint(k1.first, k1.second)
                            ));
                        }
                    }
                }
                
                // 2. Check for VDD connected to GND (Rail Short)
                if (comp->type == ComponentType::PDK_VDD || comp->type == ComponentType::PDK_VDDIO) {
                    QVector<QPointF> terms = comp->getTerminals();
                    for (const QPointF& localP : terms) {
                        QPointF sceneP = comp->mapToScene(localP);
                        QPair<int,int> key = {qRound(sceneP.x()), qRound(sceneP.y())};
                        int netID = pointToNetID_.value(key, -1);
                        
                        if (netID != -1 && netHasGround_.value(netID, false)) {
                             violations_.append(Violation(
                                ViolationType::SHORT_CIRCUIT,
                                QString("Power Rail (%1) shorted to Ground!").arg(comp->name),
                                QPoint(key.first, key.second)
                            ));
                        }
                    }
                }
            }
        }
    }
    
    struct TerminalPoint {
        QPair<int,int> pos;
        VisualComponent* component;
    };

    void checkNearMisses(QGraphicsScene* scene) {
        // Find terminals that are 1 grid step apart but not connected
        QList<TerminalPoint> terminals;
        
        for (QGraphicsItem* item : scene->items()) {
            if (auto* comp = dynamic_cast<VisualComponent*>(item)) {
                for (const QPointF& localP : comp->getTerminals()) {
                    QPointF sceneP = comp->mapToScene(localP);
                    terminals.append({
                        {qRound(sceneP.x()), qRound(sceneP.y())},
                        comp
                    });
                }
            }
        }
        
        // O(n^2) but terminals are few
        std::set<QPair<QPair<int,int>, QPair<int,int>>> reported;
        for (int i = 0; i < terminals.size(); ++i) {
            for (int j = i + 1; j < terminals.size(); ++j) {
                auto t1 = terminals[i];
                auto t2 = terminals[j];
                
                // Ignore terminals from the same component (e.g. MOSFET Gate vs Bulk)
                if (t1.component == t2.component) continue;

                auto p1 = t1.pos;
                auto p2 = t2.pos;
                
                int dx = std::abs(p1.first - p2.first);
                int dy = std::abs(p1.second - p2.second);
                
                // Near miss: exactly 1 grid step away (not connected)
                if ((dx == GRID_STEP && dy == 0) || (dx == 0 && dy == GRID_STEP)) {
                    // Check if they're on the same net
                    int net1 = pointToNetID_.value(p1, -1);
                    int net2 = pointToNetID_.value(p2, -1);
                    
                    if (net1 != net2 || net1 == -1) {
                        // Not connected - this is a near miss!
                        // Order points for consistent key
                        if (p1 > p2) std::swap(p1, p2);
                        
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
    void checkMultipleDrivers(QGraphicsScene* scene) {
        // Build map of netID -> List of Drivers
        QMap<int, QList<QGraphicsItem*>> netDrivers;
        
        for (QGraphicsItem* item : scene->items()) {
            if (auto* comp = dynamic_cast<VisualComponent*>(item)) {
                // Determine if this component IS a voltage driver
                bool isDriver = (comp->type == ComponentType::VoltageSource || 
                                 comp->type == ComponentType::VCVS ||
                                 comp->type == ComponentType::CCVS);
                                 

                if (isDriver) {
                    // Find which net it drives (Terminal 1 is usually +, Terminal 0 is -)
                    // For VSource: 1 is +
                    QVector<QPointF> terms = comp->getTerminals();
                    if (terms.size() >= 2) {
                        QPointF pPlus = comp->mapToScene(terms[1]);
                        QPair<int,int> key = {qRound(pPlus.x()), qRound(pPlus.y())};
                        if (pointToNetID_.contains(key)) {
                            int netID = pointToNetID_[key];
                            netDrivers[netID].append(comp);
                        }
                    }
                }
            }
        }
        
        // Report Conflicts
        for (auto it = netDrivers.begin(); it != netDrivers.end(); ++it) {
            if (it.value().size() > 1) {
                // Conflict!
                Violation v(
                    ViolationType::MULTIPLE_DRIVERS,
                    QString("Net is driven by %1 sources (Short Circuit risk!)").arg(it.value().size()),
                    it.value().first()->scenePos().toPoint()
                );
                v.affectedItems = it.value();
                violations_.append(v);
            }
        }
    }
    
    /**
     * @brief Check for signals crossing power domains without proper isolation.
     * Warns when components in different power domains share a net.
     */
    void checkCrossDomainSignals(QGraphicsScene* scene) {
        // Build a map of netID -> list of power domains connected to that net
        QMap<int, QSet<QString>> netDomains;
        QMap<int, QList<VisualComponent*>> netComponents;
        
        for (auto* item : scene->items()) {
            if (auto* comp = dynamic_cast<VisualComponent*>(item)) {
                QString domain;
                switch(comp->type) {
                    case ComponentType::PDK_VDD:
                        domain = "VDD";
                        break;
                    case ComponentType::PDK_VSS:
                        domain = "VSS";
                        break;
                    case ComponentType::PDK_VDDIO:
                        domain = "VDDIO";
                        break;
                    case ComponentType::PDK_VBB:
                        domain = "VBB";
                        break;
                    default:
                        continue; // Non-power components don't define domains
                }
                
                // Find which nets this power component connects to
                QVector<QPointF> terms = comp->getTerminals();
                for (const QPointF& localP : terms) {
                    QPointF sceneP = comp->mapToScene(localP);
                    int x = qRound(sceneP.x()), y = qRound(sceneP.y());
                    QPair<int,int> pt = {x, y};
                    if (pointToNetID_.contains(pt)) {
                        int netID = pointToNetID_[pt];
                        if (domain != "VSS") { // VSS is shared globally
                            netDomains[netID].insert(domain);
                            netComponents[netID].append(comp);
                        }
                    }
                }
            }
        }
        
        // Check for nets with multiple power domains
        for (auto it = netDomains.begin(); it != netDomains.end(); ++it) {
            if (it.value().size() > 1) {
                QStringList domainList = it.value().values();
                QList<QGraphicsItem*> affectedItems;
                for (auto* c : netComponents[it.key()]) {
                    affectedItems.append(c);
                }
                if (!affectedItems.isEmpty()) {
                    Violation v(
                        ViolationType::CROSS_DOMAIN_SIGNAL,
                        QString("Net connects components from different power domains: %1. Level shifter may be required.")
                            .arg(domainList.join(", ")),
                        affectedItems.first()->scenePos().toPoint()
                    );
                    v.affectedItems = affectedItems;
                    violations_.append(v);
                }
            }
        }
    }

    /**
     * @brief Detect PowerPort components not connected to any net (floating rails).
     * Emits UNDRIVEN_POWER_PORT warning (severity: Warning) for each such port.
     */
    void checkPowerPorts(QGraphicsScene* scene) {
        for (auto* item : scene->items()) {
            auto* c = dynamic_cast<VisualComponent*>(item);
            if (!c || c->type != ComponentType::PowerPort) continue;

            QVector<QPointF> terms = c->getTerminals();
            if (terms.isEmpty()) continue;

            QPointF sceneP = c->mapToScene(terms[0]);
            QPair<int,int> key = {qRound(sceneP.x()), qRound(sceneP.y())};

            // A PowerPort with no wire attached has no entry in pointToNetID_
            // or has a net with only its own contribution (count <= 1)
            int netID = pointToNetID_.value(key, -1);
            if (netID == -1 || netConnectionCount_.value(netID, 0) <= 1) {
                Violation v(
                    ViolationType::UNDRIVEN_POWER_PORT,
                    QString("PowerPort '%1' (rail: %2) is not connected to any net")
                        .arg(c->name)
                        .arg(c->params.value("railName", "?")),
                    QPoint(key.first, key.second)
                );
                v.affectedItems.append(c);
                violations_.append(v);
            }
        }
    }
};

} // namespace ERC
