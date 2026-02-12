#pragma once

#include <QGraphicsItem>
#include <QPainter>
#include <QVector>
#include "compute/orchestration/drivers/def_parser.h"

namespace acutesim::gui::editors {

/**
 * @brief Semi-transparent overlay showing physical placement on the schematic.
 *
 * Renders:
 *   - Die boundary: dashed blue rectangle, 50% opacity
 *   - Component bounding boxes: semi-transparent green fills
 *   - Instance name labels (when zoomed in)
 *
 * Follows the existing ghost/overlay pattern in GridScene (high Z-value,
 * semi-transparent, dashed pens).
 */
class PhysicalOverlayItem : public QGraphicsItem {
public:
    PhysicalOverlayItem(QGraphicsItem* parent = nullptr);

    void setDEFData(const acutesim::compute::orchestration::DEFDesign& design,
                    double pixelToMicronScale = 0.1,
                    double canvasHeight = 1000.0);

    void clear();

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;

private:
    struct CanvasPlacement {
        QString instanceName;
        QString cellName;
        QPointF canvasPos;
    };

    QRectF m_dieBounds;  // In canvas coordinates
    QVector<CanvasPlacement> m_placements;
    bool m_hasData = false;

    static constexpr double CELL_SIZE = 10.0; // Default cell box size in canvas px
};

} // namespace acutesim::gui::editors
