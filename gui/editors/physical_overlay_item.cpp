#include "physical_overlay_item.h"

namespace acutesim::gui::editors {

PhysicalOverlayItem::PhysicalOverlayItem(QGraphicsItem* parent)
    : QGraphicsItem(parent)
{
    setZValue(900); // Above components, below cursor ghosts
    setOpacity(0.5);
}

void PhysicalOverlayItem::setDEFData(
    const acutesim::compute::orchestration::DEFDesign& design,
    double pixelToMicronScale,
    double canvasHeight)
{
    using DEFParser = acutesim::compute::orchestration::DEFParser;

    prepareGeometryChange();
    m_placements.clear();

    int upm = design.unitsPerMicron;

    // Convert die area to canvas coordinates
    QPointF dieLower = DEFParser::defToCanvas(
        design.dieArea.topLeft(), canvasHeight, pixelToMicronScale, upm);
    QPointF dieUpper = DEFParser::defToCanvas(
        design.dieArea.bottomRight(), canvasHeight, pixelToMicronScale, upm);
    m_dieBounds = QRectF(dieLower, dieUpper).normalized();

    // Convert placements
    for (const auto& defPlace : design.placements) {
        CanvasPlacement cp;
        cp.instanceName = defPlace.instanceName;
        cp.cellName = defPlace.cellName;
        cp.canvasPos = DEFParser::defToCanvas(
            defPlace.defPosition, canvasHeight, pixelToMicronScale, upm);
        m_placements.append(cp);
    }

    m_hasData = true;
    update();
}

void PhysicalOverlayItem::clear() {
    prepareGeometryChange();
    m_placements.clear();
    m_dieBounds = QRectF();
    m_hasData = false;
    update();
}

QRectF PhysicalOverlayItem::boundingRect() const {
    if (!m_hasData) return QRectF();
    // Slightly enlarge for labels
    return m_dieBounds.adjusted(-20, -20, 20, 20);
}

void PhysicalOverlayItem::paint(
    QPainter* painter,
    const QStyleOptionGraphicsItem* /*option*/,
    QWidget* /*widget*/)
{
    if (!m_hasData) return;

    painter->save();

    // Die boundary: dashed blue rectangle
    QPen diePen(QColor(30, 144, 255, 128), 2, Qt::DashLine);
    painter->setPen(diePen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(m_dieBounds);

    // Die label
    painter->setPen(QColor(30, 144, 255, 180));
    painter->setFont(QFont("monospace", 8));
    painter->drawText(m_dieBounds.topLeft() + QPointF(4, -4), "DIE BOUNDARY");

    // Component boxes
    QPen cellPen(QColor(76, 175, 80, 100), 1);
    QBrush cellBrush(QColor(76, 175, 80, 40));
    painter->setPen(cellPen);
    painter->setBrush(cellBrush);

    QFont labelFont("monospace", 6);
    painter->setFont(labelFont);

    for (const auto& cp : m_placements) {
        QRectF cellRect(cp.canvasPos.x() - CELL_SIZE / 2,
                        cp.canvasPos.y() - CELL_SIZE / 2,
                        CELL_SIZE, CELL_SIZE);
        painter->drawRect(cellRect);

        // Instance name (only if reasonably zoomed in — always draw, let Qt clip)
        painter->setPen(QColor(200, 200, 200, 140));
        painter->drawText(cellRect.bottomLeft() + QPointF(0, 8), cp.instanceName);
        painter->setPen(cellPen);
    }

    painter->restore();
}

} // namespace acutesim::gui::editors
