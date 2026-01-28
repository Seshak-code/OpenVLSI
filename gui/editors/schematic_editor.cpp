#include "schematic_editor.h"
#include "component_wizard.h"
#include "../widgets/compliance_report_dialog.h"
#include "../../components/pdk_compliance.h"
#include "../../ai_api/system/crash_report.h"
#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <cmath>
#include <QDebug>
#include <QJsonDocument>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QMenu>
#include <QAction>
#include <QDateTime>
#include <QRegularExpression>
#include "../../gui/styling/theme_manager.h"

// ============================================================================
// WIRE COMPONENT
// ============================================================================
// Implementations moved to visual_component.cpp


// ============================================================================
// GRID SCENE IMPLEMENTATION
// ============================================================================
GridScene::GridScene(QObject* parent) : QGraphicsScene(parent) {
    setSceneRect(-5000, -5000, 10000, 10000);
    
    connect(this, &QGraphicsScene::selectionChanged, this, [this](){
        auto items = selectedItems();
        if (items.isEmpty()) {
            emit componentSelected(nullptr); // Clears everything
        } else {
            // Priority selection
            QGraphicsItem* item = items.first();
            if (auto* sc = dynamic_cast<VisualComponent*>(item)) {
                emit componentSelected(sc);
            } else if (auto* w = dynamic_cast<VisualWire*>(item)) {
                emit wireSelected(w);
            } else {
                emit componentSelected(nullptr);
            }
        }
    });

    // Register as Observer for Backward Sync
    designDb.addObserver(this);
}


void GridScene::drawBackground(QPainter* painter, const QRectF& rect) {
    auto& tm = acutesim::gui::styling::ThemeManager::instance();
    QColor bg = tm.getColor("schematic.background");
    if (!bg.isValid()) bg = tm.getColor("ui.background"); // Fallback
    painter->fillRect(rect, bg); 

    if (!gridVisible) return;

    // Phase 11.4: DPI-Aware & Cosmetic Rendering
    // Performance Budget: < 2ms @ 4K
    
    const int gridSize = GRID_STEP;
    const qreal left = rect.left();
    const qreal right = rect.right();
    const qreal top = rect.top();
    const qreal bottom = rect.bottom();
    
    // Calculate start/end indices aligned to grid
    const int startX = static_cast<int>(std::floor(left / gridSize)) * gridSize;
    const int endX = static_cast<int>(std::ceil(right / gridSize)) * gridSize;
    const int startY = static_cast<int>(std::floor(top / gridSize)) * gridSize;
    const int endY = static_cast<int>(std::ceil(bottom / gridSize)) * gridSize;

    // LOD Calculation via Transform
    const double scale = painter->transform().m11();
    
    // Draw Dots (Grid points)
    if (scale > 0.2) {
        // Use Cosmetic Pen width 0 (1 physical pixel)
        // Optimization: Batch point drawing
        QVarLengthArray<QPointF, 4096> points;
        
        // Adaptive density
        int step = gridSize;
        if (scale < 0.5) step *= 2;
        if (scale < 0.25) step *= 4;
        
        for (int x = startX; x <= endX; x += step) {
            for (int y = startY; y <= endY; y += step) {
                points.append(QPointF(x, y));
            }
        }
        
        QPen gridPen(tm.getColor("schematic.grid"));
        gridPen.setWidth(0); // Cosmetic (1px device independent)
        painter->setPen(gridPen);
        painter->drawPoints(points.data(), points.size());
    }

    // Draw Axes (Origin)
    QPen axisPen(tm.getColor("ui.border"));
    axisPen.setWidth(0); // 1px cosmetic
    painter->setPen(axisPen);
    painter->drawLine(0, top, 0, bottom);
    painter->drawLine(left, 0, right, 0);
}



void GridScene::clearGhostWires() {
    for (auto* wire : ghostWires) {
        if (wire && wire->scene()) {
            removeItem(wire);
        }
        delete wire;
    }
    ghostWires.clear();
}

void GridScene::setTool(ComponentType t, const QString& subName) {
    currentTool = t;
    currentSubTool = subName;
    if (ghostItem) {
        removeItem(ghostItem);
        delete ghostItem;
        ghostItem = nullptr;
    }
    
    // Cleanup any partial wiring
    isWiring = false;
    clearGhostWires();
    
    if (t != ComponentType::Select && t != ComponentType::Wire) {
        ghostItem = new VisualComponent(t);
        if (t == ComponentType::SubCircuit && !subName.isEmpty()) {
            ghostItem->subCircuitName = subName;
        }
        ghostItem->setOpacity(0.5);
        ghostItem->setOpacity(0.5);
        // Ensure ghost ignores collisions so checking code works on other stuff
        ghostItem->setZValue(100); 
        addItem(ghostItem);
    }
    
    emit toolChanged(t);
}

void GridScene::setGridVisible(bool visible) {
    gridVisible = visible;
    update();
}

void GridScene::setSnappingEnabled(bool enabled) {
    snappingEnabled = enabled;
}

bool GridScene::isWireValid(VisualWire* wire) {
    if (!wire) return false;
    QLineF l = wire->line();
    return isEndpointLegal(l.p1(), wire) && 
           isEndpointLegal(l.p2(), wire) && 
           isSegmentLegal(l, wire);
}

bool GridScene::isEndpointLegal(const QPointF& p, const VisualWire* context) {
    // Endpoints are legal if they land on a terminal, even if they touch the body.
    // If NOT on a terminal, they must NOT collide with any component body.
    
    // Check if p is on ANY terminal of ANY component
    auto allItems = items();
    for (auto* item : allItems) {
        if (auto* comp = dynamic_cast<VisualComponent*>(item)) {
            auto terminals = comp->getTerminals();
            for (const auto& localPort : terminals) {
                QPointF scenePort = comp->mapToScene(localPort);
                // Exact match (within small epsilon for float stability)
                if (QLineF(p, scenePort).length() < 0.1) {
                    return true; 
                }
            }
        }
    }

    // If not a terminal, check for body collision at endpoint
    QList<QGraphicsItem*> collisions = items(QRectF(p.x()-1, p.y()-1, 2, 2));
    for (auto* item : collisions) {
        if (auto* comp = dynamic_cast<VisualComponent*>(item)) {
            if (comp->shape().translated(comp->pos()).contains(p)) {
                return false; // Collides with body but not a terminal
            }
        }
    }
    return true;
}

bool GridScene::isSegmentLegal(const QLineF& line, const VisualWire* context) {
    // Interior of the segment must NOT intersect any component body.
    // We use a path stroker to create a thin rectangle for the segment.
    QPainterPath path;
    path.moveTo(line.p1());
    path.lineTo(line.p2());
    
    QPainterPathStroker stroker;
    stroker.setWidth(1.0); 
    QPainterPath stroke = stroker.createStroke(path);

    auto collisions = items(stroke);
    for (auto* item : collisions) {
        if (auto* comp = dynamic_cast<VisualComponent*>(item)) {
            // Check if the intersection is ONLY at the endpoints
            QPainterPath compShape = comp->shape().translated(comp->pos());
            if (compShape.intersects(stroke)) {
                // If the intersection is more than just the endpoints being on terminals, it's illegal.
                // We already checked endpoints in isEndpointLegal. 
                // Here we check if the LINE body crosses the COMP body.
                
                // Simple heuristic: if mid-point is inside, it's a collision.
                if (compShape.contains(line.pointAt(0.5))) return false;
                
                // More robust: check if segment length inside is > epsilon
                // For now, mid-point + endpoint check is usually sufficient for grid-aligned EDA.
            }
        }
    }
    return true;
}

TerminalSnapResult GridScene::findNearestTerminal(const QPointF& pos, double threshold) {
    TerminalSnapResult result;
    double minDoc = threshold;

    auto allItems = items();
    for (auto* item : allItems) {
        if (auto* comp = dynamic_cast<VisualComponent*>(item)) {
            auto terminals = comp->getTerminals();
            for (int i=0; i < terminals.size(); ++i) {
                QPointF scenePort = comp->mapToScene(terminals[i]);
                double d = QLineF(pos, scenePort).length();
                if (d < minDoc) {
                    minDoc = d;
                    result.snapped = true;
                    result.point = scenePort;
                    result.component = comp;
                    result.terminalIndex = i;
                    result.distance = d;
                }
            }
        }
    }
    return result;
}

bool GridScene::isPlacementValid(const QPointF& pos, QGraphicsItem* item) {
    QPainterPath shape;
    if (item) {
        shape = item->shape();
        shape.translate(pos); 
    } else {
        shape.addRect(pos.x()-2, pos.y()-2, 4, 4); 
    }

    auto collisions = items(shape);
    for (auto* col : collisions) {
        if (col == item || col == ghostItem || ghostWires.contains(static_cast<VisualWire*>(col))) continue;
        if (col->type() == item->type()) return false; 
    }
    return true;
}

void GridScene::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        QPointF p = snapToGrid(event->scenePos());
        
        if (currentTool == ComponentType::Select) {
            QGraphicsScene::mousePressEvent(event);
        }
        else if (currentTool == ComponentType::Wire) {
            // Apply snapping at click time
            TerminalSnapResult snap = findNearestTerminal(event->scenePos(), GRID_STEP * 0.8);
            if (snap.snapped) p = snap.point;

            if (!isWiring) {
                // START WIRING
                isWiring = true;
                wireStart = p;
                clearGhostWires();
                emit messageLogged("Started wiring.");
            } else {
                // COMMIT SEGMENTS (Click)
                // We convert current ghosts to permanent wires
                // Update wireStart to current p
                
                // Validate ghosts first
                bool allValid = true;
                for(auto* w : ghostWires) {
                    if (w->line().length() > 0 && !isWireValid(w)) {
                        allValid = false; 
                        break;
                    }
                }
                
                if (allValid) {
                     for(auto* w : ghostWires) {
                         if (w->isVisible() && w->line().length() > 0) {
                             // Create permanent copy
                             VisualWire* perm = new VisualWire(w->line());
                             addItem(perm);
                             emit topologyChanged(QDateTime::currentMSecsSinceEpoch());
                         }
                     }
                     wireStart = p;
                     // Clean to prep next segment
                     clearGhostWires();
                     emit messageLogged("Segment placed.");
                     updateERC(); // Real-time ERC after wire placement
                     
                     // Check if we hit a terminal? (Simplified: Just check if we hit a component pin)
                     // If so, finish.
                     // For now, let user double click or ESC to finish.
                } else {
                    emit messageLogged("Cannot place wire here!");
                }
            }
        }
        else {
            // Placement
            QRectF predictedBounds = ghostItem->boundingRect();
            predictedBounds.translate(p.x(), p.y());
            
            bool colliding = false;
            auto itemsAt = items(predictedBounds, Qt::IntersectsItemBoundingRect);
            for(auto* i : itemsAt) {
                 if (i != ghostItem) { 
                     // Ignore wires during component placement? Usually yes.
                     // But for now, strict.
                     if (dynamic_cast<VisualComponent*>(i)) { colliding = true; break; }
                 }
            }

            if (!colliding) {
                auto* item = new VisualComponent(currentTool);
                if (currentTool == ComponentType::SubCircuit && !currentSubTool.isEmpty()) {
                    item->subCircuitName = currentSubTool;
                }
                item->setPos(p);
                item->setRotation(ghostItem->rotation());
                addItem(item);
                emit messageLogged("Placed component.");
                emit topologyChanged(QDateTime::currentMSecsSinceEpoch());
                updateERC(); // Real-time ERC after component placement
            } else {
                emit messageLogged("Invalid Placement: Overlap!");
            }
        }
    } else if (event->button() == Qt::RightButton) {
        if (isWiring) {
            // Cancel/Finish wiring
            isWiring = false;
            clearGhostWires();
            emit messageLogged("Wiring finished.");
        } else {
            // Context Menu
            QGraphicsItem* item = itemAt(event->scenePos(), QTransform());
            if (auto* comp = dynamic_cast<VisualComponent*>(item)) {
                QMenu menu;
                QAction* rotateAction = menu.addAction("Rotate (R)");
                QAction* flipAction = menu.addAction("Flip (F)");
                QAction* deleteAction = menu.addAction("Delete (Del)");
                
                QAction* selected = menu.exec(QCursor::pos());
                if (selected == rotateAction) {
                    comp->rotate90();
                } else if (selected == flipAction) {
                    comp->flip();
                } else if (selected == deleteAction) {
                    removeItem(comp);
                    delete comp;
                    emit topologyChanged(QDateTime::currentMSecsSinceEpoch());
                }
            } else {
                 setTool(ComponentType::Select);
            }
        }
    }
}

void GridScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    // No-op for wiring now (click-click interaction)
    QGraphicsScene::mouseReleaseEvent(event);
}

void GridScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    QPointF p = snapToGrid(event->scenePos());
    emit mouseMoved((int)p.x(), (int)p.y());

    if (currentTool == ComponentType::Select) {
        QGraphicsScene::mouseMoveEvent(event);
        return;
    }

    if (currentTool == ComponentType::Wire && isWiring) {
        // TERMINAL SNAPPING (MAGNETIC)
        TerminalSnapResult snap = findNearestTerminal(event->scenePos(), GRID_STEP * 0.8);
        if (snap.snapped) {
            p = snap.point;
            // Provide visual feedback via cursor
            QGraphicsView* view = views().first();
            if (view) view->setCursor(Qt::PointingHandCursor);
        } else {
            QGraphicsView* view = views().first();
            if (view) view->setCursor(Qt::CrossCursor);
        }

        // Orthogonal L-Shape Routing
        QPointF corner(p.x(), wireStart.y());
        
        if (ghostWires.isEmpty()) {
            ghostWires.append(new VisualWire(QLineF(wireStart, wireStart))); // 1
            ghostWires.append(new VisualWire(QLineF(wireStart, wireStart))); // 2
            addItem(ghostWires[0]);
            addItem(ghostWires[1]);
        }
        
        // Update Segments
        ghostWires[0]->setLine(QLineF(wireStart, corner));
        ghostWires[1]->setLine(QLineF(corner, p));
        
        // Validity Check using new contract-based logic
        for(auto* w : ghostWires) {
            if (w->line().length() > 0) {
                 w->setVisible(true);
                 if (isWireValid(w)) w->setPen(QPen(Qt::cyan, 2));
                 else w->setPen(QPen(Qt::red, 2));
            } else {
                 w->setVisible(false); 
            }
        }
    } else if (currentTool == ComponentType::Wire && !isWiring) {
        // Cursor feedback when hovering terminals while in wire tool
        TerminalSnapResult snap = findNearestTerminal(event->scenePos(), GRID_STEP * 0.8);
        QGraphicsView* view = views().first();
        if (view) {
            if (snap.snapped) view->setCursor(Qt::PointingHandCursor);
            else view->setCursor(Qt::CrossCursor);
        }
    }
    
    if (ghostItem) {
        ghostItem->setPos(p);
        
        // ====== PLACEMENT PRE-FLIGHT: Visual Collision Indicator ======
        // Check if the current ghost position would collide with existing components.
        // If so, tint the ghost RED. Otherwise, keep it GREEN/normal.
        
        bool valid = isPlacementValid(p, ghostItem);
        if (valid) {
            ghostItem->setOpacity(0.6);
            // Apply green selection effect via flags (workaround: we will handle this in paint)
            ghostItem->setData(0, "valid"); // Custom data key for paint
        } else {
            ghostItem->setOpacity(0.4);
            ghostItem->setData(0, "invalid");
        }
        ghostItem->update(); // Force repaint
        
        // ====== GEOMETRIC CONSTRAINTS: Update Alignment Guides ======
        updateAlignmentGuides(p);
    } else {
        // Clear guides when not placing
        showAlignH = false;
        showAlignV = false;
        update();
    }
}



void GridScene::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        auto selection = selectedItems();
        for (auto* item : selection) {
            removeItem(item);
            delete item;
        }
        emit topologyChanged(QDateTime::currentMSecsSinceEpoch());
    }
    else if (event->key() == Qt::Key_Escape) {
        setTool(ComponentType::Select);
    }
    else if (event->key() == Qt::Key_R || event->key() == Qt::Key_Space) {
        if (ghostItem) {
            ghostItem->rotate90();
        } else {
            for (auto* item : selectedItems()) {
                if (auto* sc = dynamic_cast<VisualComponent*>(item)) {
                    sc->rotate90();
                }
            }
        }
    }
    else if (event->key() == Qt::Key_F) {
        if (ghostItem) {
            ghostItem->flip();
        } else {
            for (auto* item : selectedItems()) {
                if (auto* sc = dynamic_cast<VisualComponent*>(item)) {
                    sc->flip();
                }
            }
        }
    }
    else {
        QGraphicsScene::keyPressEvent(event);
    }
}

// ====== GEOMETRIC CONSTRAINTS: Alignment Guides ======
void GridScene::updateAlignmentGuides(const QPointF& ghostPos) {
    const double SNAP_THRESHOLD = 15.0; // Pixels
    
    showAlignH = false;
    showAlignV = false;
    
    // Find all existing components
    for (auto* item : items()) {
        auto* comp = dynamic_cast<VisualComponent*>(item);
        if (!comp || comp == ghostItem) continue;
        
        QPointF compCenter = comp->sceneBoundingRect().center();
        
        // Check horizontal alignment (same Y)
        if (std::abs(ghostPos.y() - compCenter.y()) < SNAP_THRESHOLD) {
            showAlignH = true;
            alignmentGuideH = QLineF(-10000, compCenter.y(), 10000, compCenter.y());
        }
        
        // Check vertical alignment (same X)
        if (std::abs(ghostPos.x() - compCenter.x()) < SNAP_THRESHOLD) {
            showAlignV = true;
            alignmentGuideV = QLineF(compCenter.x(), -10000, compCenter.x(), 10000);
        }
    }
    
    update(); // Trigger redraw
}

void GridScene::drawForeground(QPainter* painter, const QRectF& rect) {
    Q_UNUSED(rect);
    
    // Draw alignment guides
    if (showAlignH) {
        painter->setPen(QPen(QColor(255, 165, 0), 1, Qt::DashLine)); // Orange
        painter->drawLine(alignmentGuideH);
    }
    if (showAlignV) {
        painter->setPen(QPen(QColor(255, 165, 0), 1, Qt::DashLine)); // Orange
        painter->drawLine(alignmentGuideV);
    }

    // ====== TERMINAL GLOW (MAGNETIC) ======
    if (currentTool == ComponentType::Wire) {
        QGraphicsView* view = views().first();
        if (view) {
            QPointF p = view->mapToScene(view->mapFromGlobal(QCursor::pos()));
            TerminalSnapResult snap = findNearestTerminal(p, GRID_STEP * 0.8);
            if (snap.snapped) {
                painter->save();
                painter->setBrush(QColor(0, 255, 255, 128)); // Cyan glow
                painter->setPen(Qt::NoPen);
                painter->drawEllipse(snap.point, 6, 6);
                
                painter->setPen(QPen(Qt::cyan, 2));
                painter->drawLine(snap.point.x() - 10, snap.point.y(), snap.point.x() + 10, snap.point.y());
                painter->drawLine(snap.point.x(), snap.point.y() - 10, snap.point.x(), snap.point.y() + 10);
                painter->restore();
            }
        }
    }
    
    // ====== JUNCTION DOTS (Wire-to-Wire / Wire-to-Pin Branching) ======
    // Logic:
    // - Wire endpoint = 1 connection
    // - Wire middle point = 2 connections (incoming + outgoing)
    // - Pin = 1 connection
    // Dot if total >= 3
    QMap<QPair<int,int>, int> junctionCounts;
    for (auto* item : items()) {
        if (auto* w = dynamic_cast<VisualWire*>(item)) {
            if (!w->isVisible()) continue;
            QLineF l = w->line();
            QPointF p1 = w->mapToScene(l.p1());
            QPointF p2 = w->mapToScene(l.p2());
            int x1 = qRound(p1.x()), y1 = qRound(p1.y());
            int x2 = qRound(p2.x()), y2 = qRound(p2.y());
            
            if (x1 == x2) { // Vertical
                int minY = std::min(y1, y2);
                int maxY = std::max(y1, y2);
                for (int y = minY; y <= maxY; y += 20) { // Using GRID_STEP=20
                    if (y == y1 || y == y2) junctionCounts[{x1, y}] += 1; // Endpoint
                    else junctionCounts[{x1, y}] += 2; // Middle
                }
            } else if (y1 == y2) { // Horizontal
                int minX = std::min(x1, x2);
                int maxX = std::max(x1, x2);
                for (int x = minX; x <= maxX; x += 20) {
                    if (x == x1 || x == x2) junctionCounts[{x, y1}] += 1; // Endpoint
                    else junctionCounts[{x, y1}] += 2; // Middle
                }
            }
        } else if (auto* c = dynamic_cast<VisualComponent*>(item)) {
             if (c->type == ComponentType::Probe) continue; 
             for (const auto& localP : c->getTerminals()) {
                 QPointF p = c->mapToScene(localP);
                 junctionCounts[{qRound(p.x()), qRound(p.y())}] += 1;
             }
        }
    }
    
    painter->setPen(Qt::NoPen);
    painter->setBrush(acutesim::gui::styling::ThemeManager::instance().getColor("schematic.wire")); // Match wire color
    for (auto it = junctionCounts.begin(); it != junctionCounts.end(); ++it) {
        if (it.value() >= 3) {
            painter->drawEllipse(QPointF(it.key().first, it.key().second), 4, 4);
        }
    }

    // ====== CONNECTION DOTS (Wire-to-PROBE Tip Junctions) ======
    // For each PROBE TIP, check if it touches any wire.
    // If so, draw a green connection dot.
    QSet<QPair<int,int>> connectedProbePoints;
    
    for (auto* item : items()) {
        auto* tip = dynamic_cast<ProbeTip*>(item);
        if (!tip) continue;
        
        // Get tip position in scene coordinates (includes parent transform)
        QPointF sceneTip = tip->scenePos();
        int tx = qRound(sceneTip.x());
        int ty = qRound(sceneTip.y());
        
        for (auto* wItem : items()) {
            auto* wire = dynamic_cast<VisualWire*>(wItem);
            if (!wire) continue;
            
            // Convert wire endpoints to scene coordinates
            QLineF l = wire->line();
            QPointF p1 = wire->mapToScene(l.p1());
            QPointF p2 = wire->mapToScene(l.p2());
            QPointF p(tx, ty);
            
            // Distance from point to line segment
            double l2 = QLineF(p1, p2).length() * QLineF(p1, p2).length();
            double d = 0;
            if (l2 == 0) d = QLineF(p1, p).length();
            else {
                double t = ((p.x() - p1.x()) * (p2.x() - p1.x()) + (p.y() - p1.y()) * (p2.y() - p1.y())) / l2;
                t = std::max(0.0, std::min(1.0, t));
                d = QLineF(p, QPointF(p1.x() + t * (p2.x() - p1.x()), p1.y() + t * (p2.y() - p1.y()))).length();
            }
            
            if (d < 10.0) { // Larger tolerance for easier connection
                connectedProbePoints.insert({tx, ty});
                break;
            }
        }
    }
    
    // Draw the connection dots for Probes only
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0, 255, 0)); // Green
    for (const auto& pt : connectedProbePoints) {
        painter->drawEllipse(QPointF(pt.first, pt.second), 5, 5);
    }
    
    // ====== ERC VIOLATION MARKERS ======
    drawViolationMarkers(painter);
}

// ============================================================================
// ERC IMPLEMENTATION
// ============================================================================

void GridScene::updateERC() {
    if (!ercEnabled_) return;
    currentViolations_ = ercEngine_.runFullCheck(this);
    emit violationsChanged(currentViolations_);
    update(); // Trigger redraw
}

void GridScene::drawViolationMarkers(QPainter* painter) {
     int globalErrorOffset = 0;
    
    for (const auto& v : currentViolations_) {
        // --- 1. HANDLE GLOBAL SENTINEL ERRORS (Banner Style) ---
        if (v.location.x() == -9999 && v.location.y() == -9999) {
            if (views().isEmpty()) continue;
            
            // Draw a floating banner in the top-left corner of the view
            // Since drawForeground is in scene coords, we map from view (0,0)
            QRectF viewRect = views().first()->mapToScene(views().first()->viewport()->rect()).boundingRect();
            QPointF bannerPos = viewRect.topLeft() + QPointF(10, 10 + globalErrorOffset);
            
            painter->setPen(QPen(QColor(200, 50, 50), 1));
            painter->setBrush(QColor(40, 0, 0, 200));
            QRectF bannerRect(bannerPos.x(), bannerPos.y(), 300, 20);
            painter->drawRect(bannerRect);
            
            painter->setPen(Qt::white);
            QFont f = painter->font();
            f.setPixelSize(12);
            painter->setFont(f);
            painter->drawText(bannerRect.adjusted(5,0,0,0), Qt::AlignLeft | Qt::AlignVCenter, v.message);
            
            globalErrorOffset += 25;
            continue;
        }
        
        // --- 2. HANDLE LOCAL MARKERS ---
        QPoint loc = v.location;
        
        // Choose color based on severity
        QColor color;
        switch (v.severity) {
            case ERC::Severity::ERROR:
                color = QColor(255, 60, 60);   // Red
                break;
            case ERC::Severity::WARNING:
                color = QColor(255, 200, 0);   // Yellow/Orange
                break;
            case ERC::Severity::INFO:
                color = QColor(100, 180, 255); // Light blue
                break;
        }
        
        QString icon;
        switch (v.type) {
            case ERC::ViolationType::NO_GROUND:      icon = "GND"; break; // Was ⏚
            case ERC::ViolationType::FLOATING_NODE:  icon = "NC"; break;  // Was ○ (Not Connected)
            case ERC::ViolationType::FLOATING_GATE:  icon = "FLT"; break; // Was ⚡
            case ERC::ViolationType::SHORT_CIRCUIT:  icon = "SHR"; break; // Was ✖
            case ERC::ViolationType::MULTIPLE_DRIVERS: icon = "CNF"; break; // Was ⚔ (Conflict)
            case ERC::ViolationType::NEAR_MISS:      icon = "GAP"; break; // Was ↔
            case ERC::ViolationType::OPEN_DRAIN:     icon = "OD"; break;  // Was ⏒
            case ERC::ViolationType::ILLEGAL_SOURCE: icon = "BAD"; break; // Was 🚫
            case ERC::ViolationType::MISSING_PORTS:  icon = "P"; break;   // P represents Port
            case ERC::ViolationType::ILLEGAL_DRIVER: icon = "PWR"; break; // Was 🔋
            case ERC::ViolationType::MIXED_MODES:    icon = "MOD"; break; // Was !
            default:                                 icon = "?"; break;
        }
        
        QFont font = painter->font();
        font.setPixelSize(10); // Find simpler font size for text
        font.setBold(true);
        painter->setFont(font);

        QRectF textRect(loc.x() - 15, loc.y() - 10, 30, 20); // Wider rect for text
        // Add a subtle glow/outline for readability against grid
        painter->setPen(QColor(0,0,0,150));
        painter->drawText(textRect.translated(1,1), Qt::AlignCenter, icon); // Shadow
        painter->setPen(color);
        painter->drawText(textRect, Qt::AlignCenter, icon);
        
        // For near-miss violations with auto-fix, draw dashed line to target
        if (v.hasAutoFix && v.type == ERC::ViolationType::NEAR_MISS) {
            painter->setPen(QPen(QColor(100, 200, 255, 150), 1, Qt::DashLine));
            painter->drawLine(QPointF(loc), QPointF(v.suggestedTarget));
        }
    }
}

void GridScene::showRoutingHint(const ERC::Violation& v) {
    auto hint = ercAutoFix_.suggestFix(v, this);
    ercAutoFix_.showHint(this, hint);
}

void GridScene::acceptRoutingHint() {
    ercAutoFix_.acceptHint(this);
    updateERC(); // Re-check after fix
}

void GridScene::dismissRoutingHint() {
    ercAutoFix_.clearHint(this);
}

// ============================================================================
// WIRE PLACEMENT API (for AI automation)
// ============================================================================
void GridScene::placeWirePath(const QVector<QPointF>& points) {
    if (points.size() < 2) return;
    
    for (int i = 0; i < points.size() - 1; ++i) {
        QPointF p1 = snapToGrid(points[i]);
        QPointF p2 = snapToGrid(points[i + 1]);
        
        VisualWire* wire = new VisualWire(QLineF(p1, p2));
        addItem(wire);
    }
}

// ============================================================================
// NETLIST GENERATION
// ============================================================================

// Helper: Parse Value Strings (e.g. 10k, 1.2u)
static double parseValue(const QString& str) {
    if (str.isEmpty()) return 0.0;
    
    // Extract numeric part
    QString numStr;
    QString suffix;
    bool suffixFound = false;
    
    for (QChar c : str) {
        if (!suffixFound && (c.isDigit() || c == '.' || c == '-' || c == '+' || c.toLower() == 'e')) {
            numStr.append(c);
        } else {
            suffixFound = true;
            suffix.append(c);
        }
    }
    
    double val = numStr.toDouble();
    suffix = suffix.trimmed();
    
    if (suffix.startsWith("k") || suffix.startsWith("K")) val *= 1e3;
    else if (suffix.startsWith("M") && suffix.length() == 1) val *= 1e6; // Mega
    else if (suffix.startsWith("Meg")) val *= 1e6;
    else if (suffix.startsWith("G")) val *= 1e9;
    else if (suffix.startsWith("m")) val *= 1e-3;
    else if (suffix.startsWith("u")) val *= 1e-6;
    else if (suffix.startsWith("n")) val *= 1e-9;
    else if (suffix.startsWith("p")) val *= 1e-12;
    else if (suffix.startsWith("f")) val *= 1e-15;
    
    return val;
}

TensorNetlist GridScene::generateNetlist() {
    // Phase 6.1: Paradigm Bridge
    // 1. Populate/Update DesignDb from Spatial State (Migration Strategy)
    // 2. Snapshot DesignDb to TensorBlock
    
    // Clear DB to ensure perfect sync (Spatial is Truth)
    designDb.clear();
    for (auto* item : items()) {
        if (auto* c = dynamic_cast<VisualComponent*>(item)) {
            c->dbId = acutesim::components::InstUUID(); // Reset ID
        }
    }

    // --- Step 1: Connectivity Analysis (Spatial Approach) ---
    QMap<QPair<int,int>, int> pointToNetID;
    int nextNetID = 1; 
    
    // Function to get distinct net ID for a point, creating new if needed
    auto getNetID = [&](QPointF p) -> int {
        QPair<int,int> key = {(int)round(p.x()), (int)round(p.y())};
        if (!pointToNetID.contains(key)) {
            pointToNetID[key] = nextNetID++;
        }
        return pointToNetID[key];
    };
    
    // Temporary structure to merge sets: map old_id -> new_id
    QMap<int, int> merges;
    
    std::function<int(int)> findSet = [&](int id) {
        while (merges.contains(id)) id = merges[id];
        return id;
    };
    
    auto unionSets = [&](int id1, int id2) {
        int root1 = findSet(id1);
        int root2 = findSet(id2);
        if (root1 != root2) {
            if (root1 < root2) merges[root2] = root1;
            else merges[root1] = root2;
        }
    };
    
    QList<QGraphicsItem*> allItems = items();
    
    // 1.1 Traverse Wires -> Unify points
    for (auto* item : allItems) {
        if (auto* w = dynamic_cast<VisualWire*>(item)) {
            if (!w->isVisible()) continue;
            QLineF line = w->line();
            QPointF sceneP1 = w->mapToScene(line.p1());
            QPointF sceneP2 = w->mapToScene(line.p2());
            
            int x1 = (int)round(sceneP1.x()); int y1 = (int)round(sceneP1.y());
            int x2 = (int)round(sceneP2.x()); int y2 = (int)round(sceneP2.y());
            
            // Unify Start
            int startID = getNetID(QPointF(x1, y1));
            
            if (x1 == x2) { // Vertical
                int minY = std::min(y1, y2); int maxY = std::max(y1, y2);
                for (int y = minY; y <= maxY; y += GRID_STEP) unionSets(startID, getNetID(QPointF(x1, y)));
            } else if (y1 == y2) { // Horizontal
                int minX = std::min(x1, x2); int maxX = std::max(x1, x2);
                for (int x = minX; x <= maxX; x += GRID_STEP) unionSets(startID, getNetID(QPointF(x, y1)));
            } else {
                unionSets(startID, getNetID(QPointF(x2, y2)));
            }
        }
    }
    
    // 1.2 Unify Terminals
    for (auto* item : allItems) {
        if (auto* c = dynamic_cast<VisualComponent*>(item)) {
            if (c->type == ComponentType::Probe) continue;
            for (const auto& localP : c->getTerminals()) {
                getNetID(c->mapToScene(localP));
            }
        }
    }
    
    // --- Step 2: Sync to DesignDb ---
    using namespace acutesim::components;
    
    // 2.1 Map Spatial Nets to DB Nets
    QMap<int, NetUUID> spatialToDbNet;
    QMap<int, QString> spatialToName;
    
    // Predetermine names from labels
    for (auto* item : allItems) {
        if (auto* w = dynamic_cast<VisualWire*>(item)) {
            if (!w->isVisible()) continue;
            int rootID = findSet(getNetID(w->line().p1()));
            if (!w->getName().isEmpty()) {
                 emit messageLogged(QString("DEBUG: Wire ID %1 has label '%2'").arg(rootID).arg(w->getName()));
                 spatialToName[rootID] = w->getName();
            }
        }
    }
    
    auto getDbNet = [&](QPointF p) -> NetUUID {
        int rawID = getNetID(p);
        int rootID = findSet(rawID);
        if (!spatialToDbNet.contains(rootID)) {
             QString name = spatialToName.value(rootID, "n" + QString::number(rootID));
             emit messageLogged(QString("DEBUG: Creating Net %1 (Root %2)").arg(name).arg(rootID));
             spatialToDbNet[rootID] = designDb.createNet(name.toStdString());
        }
        return spatialToDbNet[rootID];
    };
    
    // Check Ground
    for (auto* item : allItems) {
        if (auto* c = dynamic_cast<VisualComponent*>(item)) {
            if (c->type == ComponentType::Ground) {
                QPointF termPos = c->mapToScene(c->getTerminals()[0]);
                int rootID = findSet(getNetID(termPos));
                if (!spatialToDbNet.contains(rootID)) {
                     // Create explicit GND net
                     spatialToDbNet[rootID] = designDb.createNet("GND");
                }
            }
        }
    }

    // Force creation of all labeled nets (even if floating)
    for (auto it = spatialToName.begin(); it != spatialToName.end(); ++it) {
        int rootID = it.key();
        if (!spatialToDbNet.contains(rootID)) {
             emit messageLogged(QString("DEBUG: Materializing floating labeled net %1 (Root %2)").arg(it.value()).arg(rootID));
             spatialToDbNet[rootID] = designDb.createNet(it.value().toStdString());
        }
    }

    // 2.2 Populate Instances
    for (auto* item : allItems) {
        if (auto* c = dynamic_cast<VisualComponent*>(item)) {
            if (c->type == ComponentType::Select || c->type == ComponentType::Wire || 
                c->type == ComponentType::Probe || c->type == ComponentType::Ground) continue;

             // Ensure Instance Exists
             if (c->dbId == InstUUID()) {
                  c->dbId = designDb.createInstance(componentTypeToString(c->type).toStdString(), 
                                                    c->name.toStdString());
             }
             
             // Sync Parameters
             double val = parseValue(c->value);
             
             std::string mainKey = "val";
             if (c->type == ComponentType::Resistor) { mainKey = "R"; designDb.setStringParam(c->dbId, "Model", c->params.value("Model", "poly_res").toStdString()); }
             else if (c->type == ComponentType::Capacitor) { mainKey = "C"; designDb.setStringParam(c->dbId, "Model", c->params.value("Model", "mim_cap").toStdString()); }
             else if (c->type == ComponentType::Inductor) { mainKey = "L"; designDb.setStringParam(c->dbId, "Model", c->params.value("Model", "spiral_ind").toStdString()); }
             else if (c->type == ComponentType::VoltageSource) mainKey = "V";
             else if (c->type == ComponentType::CurrentSource) mainKey = "I";
             else if (c->type == ComponentType::Diode) mainKey = "Is";
             
             if (!mainKey.empty()) {
                 designDb.setParam(c->dbId, mainKey, val);
             }
             
             // MOSFET Specifics
             if (c->type == ComponentType::Mosfet || c->type == ComponentType::PDK_NFET || c->type == ComponentType::PDK_PFET) {
                 designDb.setParam(c->dbId, "W", parseValue(c->params.value("W", "1u")));
                 designDb.setParam(c->dbId, "L", parseValue(c->params.value("L", "180n")));
                 designDb.setStringParam(c->dbId, "Model", c->params.value("Model", (c->type==ComponentType::PDK_PFET ? "pfet" : "nfet")).toStdString());
             }
             
             // VCVS/VCCS/CCVS/CCCS -> Gain
             if (c->type == ComponentType::VCVS || c->type == ComponentType::VCCS || 
                 c->type == ComponentType::CCVS || c->type == ComponentType::CCCS) {
                 designDb.setParam(c->dbId, "Gain", val);
             }

             // Wire up pins (Clear first to avoid dupes in this "refresh" logic)
             designDb.clearPins(c->dbId);
             
             QVector<QPointF> terms = c->getTerminals();
             for (int i = 0; i < terms.size(); ++i) {
                 QPointF p = c->mapToScene(terms[i]);
                 NetUUID netId = getDbNet(p);
                 designDb.connectPin(c->dbId, i, netId);
             }
        }
    }
    
    // 3. Snapshot
    TensorBlock block = designDb.snapshot();
    
    // Wrap in TensorNetlist
    TensorNetlist netlist;
    netlist.globalBlock = block;
    netlist.globalState.initialize(block); 
    
    return netlist;
}
// ============================================================================
// ERC (Electrical Rule Check)
// ============================================================================
QStringList GridScene::runERC() {
    // Delegate fully to the ERC Engine
    // ercEngine_ is a member object, no check needed.

    // Mode setting removed - unified component library

    // Run Check
    QList<ERC::Violation> violations = ercEngine_.runFullCheck(this);
    
    // Convert to StringList for legacy compatibility / UI display
    QStringList errors;
    for (const auto& v : violations) {
        QString prefix = (v.severity == ERC::Severity::ERROR) ? "Error" : 
                        ((v.severity == ERC::Severity::WARNING) ? "Warning" : "Info");
        errors << QString("[%1] %2").arg(prefix).arg(v.message);
    }
    
    // Trigger repaint to show markers
    update();
    
    return errors;
}

// ============================================================================
// EDITOR WIDGET
// ============================================================================
SchematicEditor::SchematicEditor(QWidget* parent) : QWidget(parent) {
    scene = new GridScene(this);
    view = new SchematicView(scene, this);
    
    setupUi();
    
    connect(scene, &GridScene::mouseMoved, this, [this](int x, int y){
        statusLabel->setText(QString("X: %1, Y: %2").arg(x).arg(y));
    });
    connect(scene, &GridScene::messageLogged, this, [this](const QString& msg){
        statusLabel->setText(msg);
    });
    connect(scene, &GridScene::toolChanged, this, [this](ComponentType t){
        if(toolButtons.contains(t)) {
            toolButtons[t]->setChecked(true);
            for(auto type : toolButtons.keys()) {
                if(type != t) toolButtons[type]->setChecked(false);
            }
             if (t == ComponentType::Select)
                view->setDragMode(QGraphicsView::RubberBandDrag);
            else
                view->setDragMode(QGraphicsView::NoDrag);
        }
    });

    connect(scene, &GridScene::componentSelected, this, &SchematicEditor::itemSelected);
    connect(scene, &GridScene::wireSelected, this, &SchematicEditor::wireSelected);
    
    // Forward topology changes
    connect(scene, &GridScene::topologyChanged, this, &SchematicEditor::topologyChanged);
    
    // Forward ERC violations
    connect(scene, &GridScene::violationsChanged, this, &SchematicEditor::violationsChanged);
    
    // Forward Drill-Down request
    connect(scene, &GridScene::requestOpenSubcircuit, this, &SchematicEditor::requestOpenSubcircuit);
}

QWidget* SchematicEditor::createRibbon() {
    QGroupBox* ribbon = new QGroupBox("Tools");
    ribbon->setStyleSheet("QGroupBox { border: 1px solid #444; margin-top: 5px; font-weight: bold; color: #BBB; } "
                          "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top center; }");
    
    QHBoxLayout* layout = new QHBoxLayout(ribbon);
    layout->setContentsMargins(5, 10, 5, 5);
    layout->setSpacing(5);

    toolButtons[ComponentType::Select] = createToolButton("Select (Esc)", ComponentType::Select);
    layout->addWidget(toolButtons[ComponentType::Select]);
    
    QFrame* line = new QFrame(); 
    line->setFrameShape(QFrame::VLine); line->setStyleSheet("color: #555;");
    layout->addWidget(line);

    toolButtons[ComponentType::Wire] = createToolButton("Wire", ComponentType::Wire);
    layout->addWidget(toolButtons[ComponentType::Wire]);
    
    // Separator before measurement tools
    QFrame* sep = new QFrame();
    sep->setFrameShape(QFrame::VLine);
    sep->setStyleSheet("color: #555;");
    layout->addWidget(sep);
    
    toolButtons[ComponentType::Probe] = createToolButton("Probe", ComponentType::Probe);
    toolButtons[ComponentType::Probe]->setStyleSheet(
        "QPushButton { background: #333; color: #FF6B6B; border: 1px solid #FF6B6B; border-radius: 3px; padding: 5px; }"
        "QPushButton:checked { background: #FF6B6B; color: white; border: 1px solid #FF8888; }"
        "QPushButton:hover { background: #444; }"
    );
    layout->addWidget(toolButtons[ComponentType::Probe]);
    
    // Separator before export tools
    QFrame* sep2 = new QFrame();
    sep2->setFrameShape(QFrame::VLine);
    sep2->setStyleSheet("color: #555;");
    layout->addWidget(sep2);
    
    // Extract as Component button (Phase 1.7)
    QPushButton* extractBtn = new QPushButton("[ ] Extract Component");
    extractBtn->setFixedHeight(35);
    extractBtn->setStyleSheet(
        "QPushButton { background: #333; color: #4CAF50; border: 1px solid #4CAF50; border-radius: 3px; padding: 5px 10px; }"
        "QPushButton:hover { background: #3a5a3a; }"
    );
    connect(extractBtn, &QPushButton::clicked, this, [this](){
        ComponentWizard wizard(this, this);
        if (wizard.exec() == QDialog::Accepted && wizard.wasSuccessful()) {
            SubCircuitDefinition def = wizard.getResult();
            // TODO: Save to library file
            // For now, just log success
            statusLabel->setText(QString("Extracted: %1 (%2 pins)")
                .arg(QString::fromStdString(def.name))
                .arg(def.pinCount));
        }
    });
    layout->addWidget(extractBtn);
    
    // PDK Audit button
    QPushButton* auditBtn = new QPushButton("🔍 Audit PDK");
    auditBtn->setFixedHeight(35);
    auditBtn->setStyleSheet(
        "QPushButton { background: #333; color: #569CD6; border: 1px solid #569CD6; border-radius: 3px; padding: 5px 10px; }"
        "QPushButton:hover { background: #264f78; }"
    );
    connect(auditBtn, &QPushButton::clicked, this, [this](){
        ComplianceReport report = PDKComplianceChecker::checkCompliance(scene);
        ComplianceReportDialog dlg(report, this);
        dlg.exec();
    });
    layout->addWidget(auditBtn);

    layout->addStretch();
    return ribbon;
}

QPushButton* SchematicEditor::createToolButton(const QString& text, ComponentType type) {
    QPushButton* btn = new QPushButton(text);
    btn->setCheckable(true);
    btn->setFixedHeight(35);
    btn->setStyleSheet(
        "QPushButton { background: #333; color: white; border: 1px solid #555; border-radius: 3px; padding: 5px; }"
        "QPushButton:checked { background: #007ACC; border: 1px solid #0099FF; }"
        "QPushButton:hover { background: #444; }"
    );
    connect(btn, &QPushButton::clicked, this, [this, type](){
        scene->setTool(type);
    });
    return btn;
}

void SchematicEditor::setupUi() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0,0,0,0);
    mainLayout->setSpacing(0);
    
    QWidget* topArea = new QWidget();
    topArea->setStyleSheet("background-color: #252526;");
    QVBoxLayout* topLayout = new QVBoxLayout(topArea);
    
    QHBoxLayout* settingsLayout = new QHBoxLayout();
    QCheckBox* gridCheck = new QCheckBox("Show Grid");
    gridCheck->setChecked(true);
    gridCheck->setStyleSheet("color: #DDD;");
    connect(gridCheck, &QCheckBox::toggled, this, &SchematicEditor::onGridToggled);
    
    QCheckBox* snapCheck = new QCheckBox("Snap to Grid");
    snapCheck->setChecked(true);
    snapCheck->setStyleSheet("color: #DDD;");
    connect(snapCheck, &QCheckBox::toggled, this, &SchematicEditor::onSnappingToggled);

    settingsLayout->addWidget(gridCheck);
    settingsLayout->addWidget(snapCheck);
    settingsLayout->addStretch();
    
    topLayout->addLayout(settingsLayout);
    topLayout->addWidget(createRibbon());
    mainLayout->addWidget(topArea);

    view->setRenderHint(QPainter::Antialiasing);
    view->setBackgroundBrush(Qt::NoBrush); 
    view->setFrameShape(QFrame::NoFrame);
    // FORCE FULL VIEWPORT UPDATE TO FIX VISUAL BUGS
    view->setViewportUpdateMode(QGraphicsView::FullViewportUpdate);

    mainLayout->addWidget(view);

    statusLabel = new QLabel("Ready");
    statusLabel->setStyleSheet("background: #007ACC; color: white; padding: 4px; font-weight: bold;");
    mainLayout->addWidget(statusLabel);
    
    scene->setTool(ComponentType::Select);
}

void SchematicEditor::onGridToggled(bool checked) {
    scene->setGridVisible(checked);
}

void SchematicEditor::onSnappingToggled(bool checked) {
    scene->setSnappingEnabled(checked);
}

// setMode() removed - unified component library

void SchematicEditor::setTool(ComponentType t, const QString& subName) {
    scene->setTool(t, subName);
}

void SchematicEditor::setWireName(VisualWire* wire, const QString& name) {
    if(wire) {
        wire->setName(name);
    }
}

QStringList SchematicEditor::getAvailableComponents() {
    // Return all available components - unified PDK library
    return {
        // Passives
        "Resistor", "Capacitor", "Inductor",
        // Sources
        "Voltage Source", "Current Source", "VCVS", "CCVS", "VCCS", "CCCS",
        // Semiconductors
        "Diode", "Schottky Diode", "N-MOSFET", "NPN BJT",
        // PDK Primitives
        "PDK_NFET", "PDK_PFET", "PDK_Resistor", "PDK_Capacitor",
        // Power
        "Ground",
        // Interfaces
        "Probe"
    };
}

void SchematicEditor::placeWirePath(const QVector<QPointF>& points) {
    // Wire placement API (for AI auto-routing)
    void placeWirePath(const QVector<QPointF>& points);
    void placePhysicallyAwareWire(const QVector<QPointF>& points, float width, double current);

    // AI Optimization API
    bool optimizeComponentRotation(const QString& id);
    
    scene->placeWirePath(points);
}

void SchematicEditor::placePhysicallyAwareWire(const QVector<QPointF>& points, float width, double current) {
    if (!scene) return;
    
    // Instead of calling scene->placeWirePath(points), we manually create wires 
    // so we can set their physics properties immediately.
    
    if (points.size() < 2) return;
    for (int i = 0; i < points.size() - 1; ++i) {
        QPointF p1 = scene->snapToGrid(points[i]);
        QPointF p2 = scene->snapToGrid(points[i+1]);
        
        VisualWire* wire = new VisualWire(QLineF(p1, p2));
        wire->setWidth(width);
        wire->maxCurrent = current;
        scene->addItem(wire);
    }

}


QJsonObject SchematicEditor::getCircuitState() {
    QJsonObject state = scene->getCircuitState();
    // mode metadata removed - unified component library
    return state;
}

void SchematicEditor::loadState(const QJsonObject& state) {
    qDebug() << "[SchematicEditor] loadState called. Keys:" << state.keys();
    if(!scene) return;
    scene->clear(); 
    
    // Components
    QJsonArray components = state["components"].toArray();
    qDebug() << "[SchematicEditor] loadState: components count =" << components.size();
    for(const auto& val : components) {
        QJsonObject c = val.toObject();
        
        QPointF pos(c["x"].toDouble(), c["y"].toDouble());
        if(scene) pos = scene->snapToGrid(pos); // Ensure alignment with wires
        double rot = c["rotation"].toDouble();
        QString type = c["type"].toString();
        
        // Prepare params
        QJsonObject params;
        for(auto it = c.begin(); it != c.end(); ++it) {
            if (it.key() == "properties") {
                QJsonObject props = it.value().toObject();
                for (auto pit = props.begin(); pit != props.end(); ++pit) {
                    params[pit.key()] = pit.value();
                }
            } else if(it.key() != "type" && it.key() != "x" && it.key() != "y" 
               && it.key() != "rotation" && it.key() != "id" && it.key() != "name") {
                params[it.key()] = it.value();   
            }
        }
        
        QString newId = placeComponent(type, pos, rot, params);
        
        // Restore Name/ID
        if (VisualComponent* comp = findComponent(newId)) {
            QString targetName = c["name"].toString();
            if (targetName.isEmpty()) targetName = c["id"].toString();
            
            if (!targetName.isEmpty()) {
                comp->name = targetName;
            }
            
            // ICPackage: Load pins from source schematic
            if (comp->type == ComponentType::ICPackage) {
                qDebug() << "[ICPackage] Processing ICPackage component:" << comp->name;
                QString srcFile = params["sourceFile"].toString();
                qDebug() << "[ICPackage] sourceFile from params:" << srcFile;
                
                if (!srcFile.isEmpty()) {
                    comp->sourceFilePath = srcFile;
                    // Resolve path relative to resources or user_libs
                    QStringList searchPaths = {
                        srcFile,
                        "resources/cells/" + srcFile,
                        "user_libs/" + srcFile
                    };
                    bool found = false;
                    for (const QString& path : searchPaths) {
                        qDebug() << "[ICPackage] Checking path:" << path << "exists:" << QFileInfo::exists(path);
                        if (QFileInfo::exists(path)) {
                            comp->loadPinsFromSchematic(path);
                            comp->sourceFilePath = path; // FIX: Store resolved path for drill-down
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        qDebug() << "[ICPackage] WARNING: Could not find source file:" << srcFile;
                    }
                } else {
                    qDebug() << "[ICPackage] No sourceFile specified in properties";
                }
                // Also set display name from params
                if (params.contains("displayName")) {
                    comp->displayName = params["displayName"].toString();
                    qDebug() << "[ICPackage] Set displayName:" << comp->displayName;
                }
            }
        }
    }
    
    // Wires
    QJsonArray wires = state["wires"].toArray();
    for(const auto& val : wires) {
        QJsonObject w = val.toObject();
        QJsonArray pointsArr = w["points"].toArray();
        QVector<QPointF> points;
        for(const auto& p : pointsArr) {
            QJsonObject pt = p.toObject();
            points.append(QPointF(pt["x"].toDouble(), pt["y"].toDouble()));
        }
        if (points.size() >= 2) {
            scene->placeWirePath(points);
        }
    }
    
    if(scene) scene->triggerTopologyUpdate(); // Force ERC update
}

QString SchematicEditor::dumpSceneState() {
    QJsonObject state = getCircuitState();
    
    // Dump Components
    QString dump;
    QJsonArray components = state["components"].toArray();
    for (const auto& val : components) {
        QJsonObject c = val.toObject();
        dump += QString("COMPONENT: %1 Type:%2 Pos:(%3,%4) Rot:%5 Value:%6\n")
                .arg(c["name"].toString())
                .arg(c["type"].toString())
                .arg(c["x"].toDouble()).arg(c["y"].toDouble())
                .arg(c["rotation"].toDouble())
                .arg(c["properties"].toObject()["value"].toString());
    }
    
    // Dump Wires
    QJsonArray wires = state["wires"].toArray();
    for (const auto& val : wires) {
        QJsonObject w = val.toObject();
        QJsonArray points = w["points"].toArray();
        if (points.size() >= 2) {
             QJsonObject p1 = points[0].toObject();
             QJsonObject p2 = points[points.size()-1].toObject();
             dump += QString("WIRE: Net:%1 P1:(%2,%3) P2:(%4,%5)\n")
                     .arg(w["netName"].toString())
                     .arg(p1["x"].toDouble()).arg(p1["y"].toDouble())
                     .arg(p2["x"].toDouble()).arg(p2["y"].toDouble());
        }
    }
    
    return dump;
}

// ============================================================================
// GRID SCENE JSON SERIALIZATION
// ============================================================================
QJsonObject GridScene::getCircuitState() const {
    QJsonObject root;
    
    // 1. Components
    QJsonArray components;
    for (auto* item : items()) {
        if (auto* c = dynamic_cast<VisualComponent*>(item)) {
            QJsonObject compInfo;
            compInfo["id"] = c->name; // Use component Name as stable ID (e.g. R1)
            compInfo["name"] = c->name;
            compInfo["type"] = componentTypeToString(c->type); 
            compInfo["x"] = c->pos().x();
            compInfo["y"] = c->pos().y();
            compInfo["rotation"] = c->rotation();
            
            QJsonObject props;
            props["value"] = c->value;
            // Serialize all dynamic parameters (e.g. W, L, Model)
            for(auto it = c->params.begin(); it != c->params.end(); ++it) {
                props[it.key()] = it.value();
            }
            compInfo["properties"] = props;
            
            components.append(compInfo);
        }
    }
    root["components"] = components;
    
    // 2. Wires
    QJsonArray wires;
    for (auto* item : items()) {
        if (auto* w = dynamic_cast<VisualWire*>(item)) {
             QJsonObject wireInfo;
             wireInfo["netName"] = w->getName().isEmpty() ? "UNNAMED" : w->getName();
             
             QJsonArray points;
             QLineF line = w->line();
             
             QJsonObject p1, p2;
             p1["x"] = line.p1().x(); p1["y"] = line.p1().y();
             p2["x"] = line.p2().x(); p2["y"] = line.p2().y();
             
             points.append(p1);
             points.append(p2);
             wireInfo["points"] = points;
             
             wires.append(wireInfo);
        }
    }
    root["wires"] = wires;
    
    // 3. Metadata
    QJsonObject meta;
    meta["gridVisible"] = gridVisible;
    meta["snappingEnabled"] = snappingEnabled;
    
    // Pass mode from editor if possible, or just add it here
    // For now, let's assume we'll inject it from SchematicEditor::getCircuitState
    root["metadata"] = meta;
    
    return root;
}

bool GridScene::isOccupied(const QRectF& rect, QGraphicsItem* exclude) {
    // Search for items in this region
    QList<QGraphicsItem*> itemsInRect = items(rect, Qt::IntersectsItemBoundingRect);
    
    // Debug logging for empty scene issues
    if (itemsInRect.size() > 0) {
        // qDebug() << "Checking collision at:" << rect << "Found" << itemsInRect.size() << "items.";
    }

    for (auto* item : itemsInRect) {
        // Exclude self and ghost item
        if (item == exclude) continue;
        if (item == ghostItem) {
             // qDebug() << "Ignoring ghostItem at" << item->pos();
             continue;
        }
        
        // Exclude ghost wires
        bool isGhostWire = false;
        for (auto* gw : ghostWires) {
            if (item == gw) { isGhostWire = true; break; }
        }
        if (isGhostWire) continue;
        
        // Only count components and wires as "obstructions"
        if (auto* vc = dynamic_cast<VisualComponent*>(item)) {
            qDebug() << "COLLISION: Component" << vc->name << "(" << componentTypeToString(vc->type) << ") at" << vc->pos()
                     << "Bounds:" << vc->sceneBoundingRect() << "vs Check:" << rect;
            return true;
        } 
        else if (auto* vw = dynamic_cast<VisualWire*>(item)) {
            qDebug() << "COLLISION: Wire" << vw->getName() << "Line:" << vw->line() << "vs Check:" << rect;
            return true;
        }
    }
    return false;
}

QStringList SchematicEditor::runERC() {
    return scene->runERC();
}

// ============================================================================
// QUICK PREVIEW TOGGLE
// ============================================================================
void GridScene::setQuickPreviewEnabled(bool enabled) {
    if (quickPreviewEnabled_ != enabled) {
        quickPreviewEnabled_ = enabled;
    }
}

void SchematicEditor::setQuickPreviewEnabled(bool enabled) {
    quickPreviewEnabled_ = enabled;
    if (GridScene* s = qobject_cast<GridScene*>(scene)) {
        s->setQuickPreviewEnabled(enabled);
    }
}

// ============================================================================
// HIERARCHICAL SCHEMATICS (Phase 2)
// ============================================================================

bool SchematicEditor::saveToFile(const QString& filePath) {
    QFile saveFile(filePath);
    if (!saveFile.open(QIODevice::WriteOnly)) {
        qWarning() << "Could not open file for writing:" << filePath;
        return false;
    }
    QJsonObject state = getCircuitState();
    QJsonDocument saveDoc(state);
    saveFile.write(saveDoc.toJson());
    return true;
}

bool SchematicEditor::loadFromFile(const QString& filePath) {
    QFile loadFile(filePath);
    if (!loadFile.open(QIODevice::ReadOnly)) {
        qWarning() << "Could not open file for reading:" << filePath;
        return false;
    }
    QByteArray saveData = loadFile.readAll();
    QJsonDocument loadDoc(QJsonDocument::fromJson(saveData));
    if(loadDoc.isNull() || !loadDoc.isObject()){
        qWarning() << "Invalid JSON in file:" << filePath;
        return false;
    }
    
    scene->clear(); 
    loadState(loadDoc.object());
    return true;
}

void GridScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    QGraphicsItem* item = itemAt(event->scenePos(), QTransform());
    if (auto* comp = dynamic_cast<VisualComponent*>(item)) {
        // 1. File-Based Drill Down
        if (!comp->sourceFilePath.isEmpty()) {
             emit requestOpenSubcircuit(comp->sourceFilePath);
             return; // Consume event
        }
        
        // 2. Standard Cell Drill Down (Migration)
        if (comp->type == ComponentType::SubCircuit || comp->type == ComponentType::ICPackage) {
             // Heuristic: Check resources/cells/NAME.sch
             QString cleanName = comp->subCircuitName.toLower().replace(" ", "_");
             // Clean special chars
             cleanName.remove(QRegularExpression("[^a-zA-Z0-9_]"));
             
             QStringList paths = {
                 "resources/cells/" + cleanName + ".sch",
                 "user_libs/" + cleanName + ".sch"
             };
             
             for(const auto& p : paths) {
                 if (QFileInfo::exists(p)) {
                     emit requestOpenSubcircuit(p);
                     return;
                 }
             }
        }
    }
    QGraphicsScene::mouseDoubleClickEvent(event);
}

void GridScene::contextMenuEvent(QGraphicsSceneContextMenuEvent* event) {
    QGraphicsItem* item = itemAt(event->scenePos(), QTransform());
    if (auto* comp = dynamic_cast<VisualComponent*>(item)) {
        // Check if component supports drill-down
        QString path = comp->sourceFilePath;
        
        // If path is empty, try heuristic for SubCircuits/ICPackages
        if (path.isEmpty() && (comp->type == ComponentType::SubCircuit || comp->type == ComponentType::ICPackage)) {
             QString cleanName = comp->subCircuitName.toLower().replace(" ", "_");
             cleanName.remove(QRegularExpression("[^a-zA-Z0-9_]"));
             
             QStringList paths = {
                 "resources/cells/" + cleanName + ".sch",
                 "user_libs/" + cleanName + ".sch"
             };
             
             for(const auto& p : paths) {
                 if (QFileInfo::exists(p)) {
                     path = p;
                     break;
                 }
             }
        }
        
        if (!path.isEmpty()) {
            QMenu menu;
            QAction* drillDown = menu.addAction(QString("Open Schematic (%1)").arg(comp->name));
            connect(drillDown, &QAction::triggered, this, [this, path](){
                emit requestOpenSubcircuit(path);
            });
            menu.exec(event->screenPos());
            return;
        }
    }
    QGraphicsScene::contextMenuEvent(event);
}

void SchematicEditor::createSubcircuitFromSelection() {
    // Scaffold for Phase 2.3
    QList<QGraphicsItem*> selected = scene->selectedItems();
    if (selected.isEmpty()) {
        QMessageBox::information(this, "Encapsulate", "Please select components to encapsulate.");
        return;
    }
    
    // For now, just log intent
    qDebug() << "Encapsulate requested for" << selected.size() << "items.";
    QMessageBox::information(this, "Encapsulate", "Subcircuit functionality coming in Phase 2.3!");
}

QPointF SchematicEditor::findClearSpace(QPointF idealPos, QSizeF size, int searchRadius) const {
    GridScene* gs = qobject_cast<GridScene*>(scene);
    if (!gs) return idealPos;

    QRectF testRect(idealPos, size);
    
    // 0. Check Ideal (Deterministic: Always check first)
    if (!gs->collidesWithAny(testRect) && gs->followsPDKRules(testRect)) {
        return idealPos;
    }

    // 1. Clockwise Spiral Search (Deterministic)
    // Radius in grid steps (20px)
    int step = gs->getGridSnap(); 
    if (step <= 0) step = 20;
    
    for (int r = 1; r <= searchRadius; r++) {
        // Perimeter search for box of size 2*r
        // Start Top-Left (-r, -r) relative to ideal
        // Order: Top Edge (L->R), Right Edge (T->B), Bottom Edge (R->L), Left Edge (B->T)
        // Coords relative to idealPos in grid steps
        
        // 1. Top Edge: (-r, -r) to (r, -r)
        for (int x = -r; x < r; x++) {
            QPointF trial = idealPos + QPointF(x * step, -r * step);
            testRect.moveTopLeft(trial);
            if (!gs->collidesWithAny(testRect) && gs->followsPDKRules(testRect)) return trial;
        }
        
        // 2. Right Edge: (r, -r) to (r, r)
        for (int y = -r; y < r; y++) {
            QPointF trial = idealPos + QPointF(r * step, y * step);
            testRect.moveTopLeft(trial);
            if (!gs->collidesWithAny(testRect) && gs->followsPDKRules(testRect)) return trial;
        }
        
        // 3. Bottom Edge: (r, r) to (-r, r)
        for (int x = r; x > -r; x--) {
            QPointF trial = idealPos + QPointF(x * step, r * step);
            testRect.moveTopLeft(trial);
            if (!gs->collidesWithAny(testRect) && gs->followsPDKRules(testRect)) return trial;
        }
        
        // 4. Left Edge: (-r, r) to (-r, -r)
        for (int y = r; y > -r; y--) {
            QPointF trial = idealPos + QPointF(-r * step, y * step);
            testRect.moveTopLeft(trial);
            if (!gs->collidesWithAny(testRect) && gs->followsPDKRules(testRect)) return trial;
        }
    }
    
    // Fallback: Return ideal (user must resolve collision)
    return idealPos;
}

void SchematicEditor::placeNextTo(const QString& refId, const QString& newType, Direction dir) {
    // 1. Find Reference
    VisualComponent* refComp = nullptr;
    for (auto* item : scene->items()) {
        if (auto* vc = dynamic_cast<VisualComponent*>(item)) {
            if (vc->name == refId) {
                refComp = vc;
                break;
            }
        }
    }
    
    if (!refComp) {
        qDebug() << "Error: Reference component '" + refId + "' not found.";
        return;
    }
    
    // 2. Calculate Ideal Position with Pin Alignment
    QRectF refBox = refComp->sceneBoundingRect();
    QPointF idealPos = refBox.topLeft();
    
    // New Component Size Estimation (Hardcoded for now, TODO: Fetch from library)
    QSizeF newSize(100, 60); 
    
    // Default Spacing
    double spacing = 100.0;
    
    switch (dir) {
        case Direction::East: 
            idealPos.rx() += refBox.width() + spacing;
            // Align Y Center
            idealPos.ry() += (refBox.height() - newSize.height()) / 2.0;
            break;
            
        case Direction::West:  
            idealPos.rx() -= (newSize.width() + spacing); 
            // Align Y Center
            idealPos.ry() += (refBox.height() - newSize.height()) / 2.0;
            break;
            
        case Direction::North: 
            idealPos.ry() -= (newSize.height() + spacing); 
            // Align X Center
            idealPos.rx() += (refBox.width() - newSize.width()) / 2.0;
            break;
            
        case Direction::South: 
            idealPos.ry() += refBox.height() + spacing; 
            // Align X Center
            idealPos.rx() += (refBox.width() - newSize.width()) / 2.0;
            break;
    }
    
    // Snap to Grid (Coarse)
    int grid = 20;
    idealPos.setX(round(idealPos.x() / grid) * grid);
    idealPos.setY(round(idealPos.y() / grid) * grid);
    
    // 3. Find Clear Space (Deterministic)
    QPointF validPos = findClearSpace(idealPos, newSize); 
    
    // 4. Place
    placeComponent(newType, validPos, 0, QJsonObject());
}

// ============================================================================



// ============================================================================
// DESIGNDB OBSERVER (Backward Sync)
// ============================================================================

static ComponentType mapStringToType(const QString& s) {
    if (s == "Resistor" || s == "R") return ComponentType::Resistor;
    if (s == "Capacitor" || s == "C") return ComponentType::Capacitor;
    if (s == "Inductor" || s == "L") return ComponentType::Inductor;
    if (s == "VoltageSource" || s == "V") return ComponentType::VoltageSource;
    if (s == "CurrentSource" || s == "I") return ComponentType::CurrentSource;
    if (s == "Diode" || s == "D") return ComponentType::Diode;
    if (s == "Mosfet" || s == "M") return ComponentType::Mosfet;
    if (s == "Bjt" || s == "Q") return ComponentType::Bjt;
    if (s == "Ground" || s == "0") return ComponentType::Ground;
    return ComponentType::Select; // Default / Fail
}

void GridScene::onNetModified(const acutesim::components::Net& net) {
    // If net name changed, update labels?
    // Not implemented in VisualWire yet, but placeholder for now.
    emit messageLogged(QString("Db Sync: Net %1 modified").arg(QString::fromStdString(net.name)));
}

void GridScene::ofInstancePlaced(const acutesim::components::Instance& inst) {
    // If an instance is placed via API/Script, we need to create a VisualComponent
    
    // Check if we already have it (avoid infinite loop if we triggered it)
    for (auto* item : items()) {
        if (auto* vc = dynamic_cast<VisualComponent*>(item)) {
            if (vc->dbId == inst.id) return; // Already exists
        }
    }
    
    // Create new visual component
    ComponentType type = mapStringToType(QString::fromStdString(inst.masterName));
    if (type == ComponentType::Select) return; // Unknown
    
    VisualComponent* vc = new VisualComponent(type);
    vc->dbId = inst.id;
        vc->name = QString::fromStdString(inst.name);
    vc->setPos(inst.spatial.x, inst.spatial.y);
    vc->setRotation(inst.spatial.rotation);
    
    // Sync Params
    for(auto it = inst.parameters.begin(); it != inst.parameters.end(); ++it) {
        vc->params.insert(QString::fromStdString(it->first), QString::number(it->second));
        // Special case for main value
        if (it->first == "val" || it->first == "R" || it->first == "C" || it->first == "L") {
             vc->value = QString::number(it->second);
             if (it->first == "val") { /* nothing */ }
             else if (it->first == "R") vc->value += "k"; // heuristic? No, raw number.
             else if (it->first == "C") vc->value += "u";
        }
    }
    
    addItem(vc);
    update();
}

void GridScene::onInstanceMoved(const acutesim::components::Instance& inst) {
    for (auto* item : items()) {
        if (auto* vc = dynamic_cast<VisualComponent*>(item)) {
            if (vc->dbId == inst.id) {
                vc->setPos(inst.spatial.x, inst.spatial.y);
                vc->setRotation(inst.spatial.rotation);
                update();
                return;
            }
        }
    }
}

void GridScene::onTopologyInvalidated() {
    // For now we don't do full re-placement, just update.
    emit messageLogged("Db Sync: Topology invalidated. Re-generating netlist...");
    // Potentially trigger re-scan or invalidation visual
}

void GridScene::onNetRenamed(acutesim::components::NetUUID id, const std::string& oldName, const std::string& newName) {
    Q_UNUSED(id);
    QString qOld = QString::fromStdString(oldName);
    QString qNew = QString::fromStdString(newName);
    
    // Find wires with this name and update them
    for (auto* item : items()) {
        if (auto* w = dynamic_cast<VisualWire*>(item)) {
            if (w->getName() == qOld) {
                w->setName(qNew);
            }
        }
    }
    update();
}

// ============================================================================
// SPATIAL INTELLIGENCE
// ============================================================================

bool GridScene::collidesWithAny(const QRectF& rect, const QGraphicsItem* ignore) const {
    // Get all items in the rect
    QList<QGraphicsItem*> collisions = items(rect, Qt::IntersectsItemBoundingRect);
    for (auto* item : collisions) {
        if (item == ignore) continue;
        
        // Only care about Components and Wires (VisualWire)
        if (dynamic_cast<VisualComponent*>(item) || dynamic_cast<VisualWire*>(item)) {
            // Precise shape check if needed, but rect is usually safer for placement
            if (item->sceneBoundingRect().intersects(rect)) {
                return true;
            }
        }
    }
    return false;
}

bool GridScene::followsPDKRules(const QRectF& rect) const {
    // Default PDK Die Area: -5000 to +5000 (microns/units)
    // TODO: Fetch from actual PDK Manager
    QRectF dieArea(-5000, -5000, 10000, 10000);
    return dieArea.contains(rect);
}

bool GridScene::isWireAllowed(const QLineF& line) const {
    // Check against Forbidden Zones
    // For now, assume any long wire crossing the origin (0,0) is risky if that's a central block
    // Placeholder logic
    return true; 
}
