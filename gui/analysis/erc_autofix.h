#pragma once

#include "../components/visual_component.h"
#include "erc_engine.h"
#include <QGraphicsScene>
#include <QVector>
#include <QPoint>
#include <QRectF>
#include <queue>
#include <unordered_map>
#include <functional>

/**
 * erc_autofix.h
 * 
 * Intelligent Auto-Fix Suggestions for ERC Violations
 * 
 * Features:
 * - A* pathfinding on grid graph
 * - Collision avoidance with components
 * - Die area boundary constraints
 * - Code-completion style visual hints
 */

namespace ERC {

// ============================================================================
// ROUTING HINT
// ============================================================================

struct RoutingHint {
    QVector<QPoint> suggestedPath;
    double routingCost;             // Lower is better
    bool avoidsCollisions;
    QString description;
    
    RoutingHint() : routingCost(0), avoidsCollisions(true) {}
};

// ============================================================================
// A* PATHFINDER
// ============================================================================

class GridPathfinder {
public:
    struct Node {
        QPoint pos;
        double gCost;   // Cost from start
        double hCost;   // Heuristic cost to goal
        double fCost() const { return gCost + hCost; }
        QPoint parent;
        
        bool operator>(const Node& other) const {
            return fCost() > other.fCost();
        }
    };
    
    GridPathfinder(int gridStep = GRID_STEP)
        : gridStep_(gridStep) {}
    
    /**
     * Find optimal path from start to end avoiding obstacles
     */
    QVector<QPoint> findPath(
        QPoint start, 
        QPoint end,
        const QList<QRectF>& obstacles,
        const QRectF& bounds = QRectF())
    {
        // Hash function for QPoint
        auto pointHash = [](const QPoint& p) {
            return std::hash<int>()(p.x()) ^ (std::hash<int>()(p.y()) << 16);
        };
        
        std::priority_queue<Node, std::vector<Node>, std::greater<Node>> openSet;
        std::unordered_map<size_t, bool> closedSet;
        std::unordered_map<size_t, Node> allNodes;
        
        // Start node
        Node startNode;
        startNode.pos = start;
        startNode.gCost = 0;
        startNode.hCost = heuristic(start, end);
        startNode.parent = QPoint(-1, -1);
        
        openSet.push(startNode);
        allNodes[pointHash(start)] = startNode;
        
        // Direction vectors (4-connected grid)
        const QPoint dirs[] = {
            {gridStep_, 0}, {-gridStep_, 0},
            {0, gridStep_}, {0, -gridStep_}
        };
        
        while (!openSet.empty()) {
            Node current = openSet.top();
            openSet.pop();
            
            size_t currentHash = pointHash(current.pos);
            
            // Skip if already processed
            if (closedSet.count(currentHash)) continue;
            closedSet[currentHash] = true;
            
            // Check if we reached the goal
            if (current.pos == end) {
                return reconstructPath(allNodes, current, pointHash);
            }
            
            // Explore neighbors
            for (const auto& dir : dirs) {
                QPoint neighbor = current.pos + dir;
                size_t neighborHash = pointHash(neighbor);
                
                // Skip if already visited
                if (closedSet.count(neighborHash)) continue;
                
                // Check bounds
                if (!bounds.isNull() && !bounds.contains(neighbor)) continue;
                
                // Check obstacles
                if (collidesWithObstacles(neighbor, obstacles)) continue;
                
                // Calculate costs
                double turnPenalty = 0.0;
                if (current.parent != QPoint(-1, -1)) {
                    QPoint prevDir = current.pos - current.parent;
                    QPoint newDir = neighbor - current.pos;
                    if (prevDir != newDir) {
                        turnPenalty = gridStep_ * 0.5; // Penalize turns
                    }
                }
                
                double tentativeG = current.gCost + gridStep_ + turnPenalty;
                
                // Check if this path is better
                if (!allNodes.count(neighborHash) || tentativeG < allNodes[neighborHash].gCost) {
                    Node neighborNode;
                    neighborNode.pos = neighbor;
                    neighborNode.gCost = tentativeG;
                    neighborNode.hCost = heuristic(neighbor, end);
                    neighborNode.parent = current.pos;
                    
                    allNodes[neighborHash] = neighborNode;
                    openSet.push(neighborNode);
                }
            }
        }
        
        // No path found
        return QVector<QPoint>();
    }

private:
    int gridStep_;
    
    double heuristic(QPoint a, QPoint b) {
        // Manhattan distance
        return std::abs(a.x() - b.x()) + std::abs(a.y() - b.y());
    }
    
    bool collidesWithObstacles(QPoint pos, const QList<QRectF>& obstacles) {
        // Check if point is inside any obstacle
        for (const auto& obs : obstacles) {
            if (obs.contains(QPointF(pos))) {
                return true;
            }
        }
        return false;
    }
    
    QVector<QPoint> reconstructPath(
        const std::unordered_map<size_t, Node>& allNodes,
        const Node& endNode,
        std::function<size_t(const QPoint&)> hashFn)
    {
        QVector<QPoint> path;
        QPoint current = endNode.pos;
        
        while (current != QPoint(-1, -1)) {
            path.prepend(current);
            size_t hash = hashFn(current);
            if (allNodes.count(hash) && allNodes.at(hash).parent != QPoint(-1, -1)) {
                current = allNodes.at(hash).parent;
            } else {
                break;
            }
        }
        
        return path;
    }
};

// ============================================================================
// ERC AUTO-FIX ENGINE
// ============================================================================

class ERCAutoFix {
public:
    ERCAutoFix() = default;
    
    /**
     * Generate routing hints for a near-miss violation
     */
    RoutingHint suggestFix(
        const Violation& violation,
        QGraphicsScene* scene,
        const QRectF& dieArea = QRectF())
    {
        RoutingHint hint;
        
        if (!violation.hasAutoFix) {
            hint.description = "No auto-fix available for this violation.";
            return hint;
        }
        
        // Collect obstacles (component bounding boxes)
        QList<QRectF> obstacles;
        for (QGraphicsItem* item : scene->items()) {
            if (auto* comp = dynamic_cast<VisualComponent*>(item)) {
                obstacles.append(comp->sceneBoundingRect().adjusted(-2, -2, 2, 2));
            }
        }
        
        // Find path
        GridPathfinder pathfinder;
        QVector<QPoint> path = pathfinder.findPath(
            violation.location,
            violation.suggestedTarget,
            obstacles,
            dieArea
        );
        
        if (path.isEmpty()) {
            hint.description = "No valid routing path found (obstacles blocking).";
            hint.avoidsCollisions = false;
        } else {
            hint.suggestedPath = path;
            hint.routingCost = calculatePathCost(path);
            hint.avoidsCollisions = true;
            hint.description = QString("Connect (%1,%2) to (%3,%4) - %5 segments")
                .arg(violation.location.x()).arg(violation.location.y())
                .arg(violation.suggestedTarget.x()).arg(violation.suggestedTarget.y())
                .arg(path.size() - 1);
        }
        
        return hint;
    }
    
    /**
     * Check if a hint is currently being shown
     */
    bool isHintActive() const { return hintActive_; }
    
    /**
     * Show hint visually (creates ghost wires)
     */
    void showHint(QGraphicsScene* scene, const RoutingHint& hint) {
        clearHint(scene);
        
        if (hint.suggestedPath.size() < 2) return;
        
        hintActive_ = true;
        currentHint_ = hint;
        
        // Create ghost wire segments
        for (int i = 0; i < hint.suggestedPath.size() - 1; ++i) {
            QPoint p1 = hint.suggestedPath[i];
            QPoint p2 = hint.suggestedPath[i + 1];
            
            QGraphicsLineItem* ghost = new QGraphicsLineItem(
                QLineF(QPointF(p1), QPointF(p2))
            );
            ghost->setPen(QPen(QColor(0, 200, 200, 150), 3, Qt::DashLine));
            ghost->setZValue(1000);
            ghost->setData(0, "erc_hint"); // Tag for identification
            scene->addItem(ghost);
            hintItems_.append(ghost);
        }
        
        // TODO: Add "Press Tab to accept" tooltip
    }
    
    /**
     * Accept the current hint (create real wires)
     */
    void acceptHint(QGraphicsScene* scene) {
        if (!hintActive_ || currentHint_.suggestedPath.size() < 2) return;
        
        // Create permanent wires
        for (int i = 0; i < currentHint_.suggestedPath.size() - 1; ++i) {
            QPoint p1 = currentHint_.suggestedPath[i];
            QPoint p2 = currentHint_.suggestedPath[i + 1];
            
            VisualWire* wire = new VisualWire(QLineF(QPointF(p1), QPointF(p2)));
            scene->addItem(wire);
        }
        
        clearHint(scene);
    }
    
    /**
     * Dismiss the current hint
     */
    void clearHint(QGraphicsScene* scene) {
        for (auto* item : hintItems_) {
            scene->removeItem(item);
            delete item;
        }
        hintItems_.clear();
        hintActive_ = false;
        currentHint_ = RoutingHint();
    }

private:
    bool hintActive_ = false;
    RoutingHint currentHint_;
    QList<QGraphicsLineItem*> hintItems_;
    
    double calculatePathCost(const QVector<QPoint>& path) {
        if (path.size() < 2) return 0;
        
        double cost = 0;
        for (int i = 0; i < path.size() - 1; ++i) {
            cost += std::abs(path[i].x() - path[i+1].x()) + 
                    std::abs(path[i].y() - path[i+1].y());
        }
        
        // Add turn penalty
        for (int i = 1; i < path.size() - 1; ++i) {
            QPoint d1 = path[i] - path[i-1];
            QPoint d2 = path[i+1] - path[i];
            if (d1 != d2) cost += GRID_STEP * 0.5;
        }
        
        return cost;
    }
};

} // namespace ERC
