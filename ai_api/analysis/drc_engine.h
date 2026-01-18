#pragma once
#include "schema_api.h"
#include <QVector>
#include <QPolygonF>
#include <QMap>

namespace AIAPI {

/**
 * Advanced DRCEngine supporting foundry-grade verification.
 * Implements OpenDRC-style checks for Density, Antenna, and EOL.
 */
class DRCEngine {
public:
    DRCEngine();

    /**
     * Load layout geometry from the Schematic Editor.
     * Converts visual components and wires into raw polygon layers.
     */
    void loadLayout();

    // === Core Verification Suite ===
    
    QVector<DRCViolation> runBasicRules(const QVector<DRCRule>& rules);
    QVector<DRCViolation> runDensityChecks(const QVector<DensityRule>& rules);
    QVector<DRCViolation> runAntennaChecks(const QVector<AntennaRule>& rules);
    QVector<DRCViolation> runEOLChecks(const QVector<EOLRule>& rules);

    /**
     * Runs the full suite of checks and aggregates violations.
     */
    QVector<DRCViolation> runFullSuite(const QVector<DRCRule>& basic,
                                       const QVector<DensityRule>& density,
                                       const QVector<AntennaRule>& antenna,
                                       const QVector<EOLRule>& eol);

private:
    // Geometry Kernels
    double calculatePolygonArea(const QPolygonF& poly);
    QVector<QPolygonF> getMetalShapes(const QString& layer);
    
    // Layer Abstraction: LayerName -> List of Polygons
    QMap<QString, QVector<QPolygonF>> m_layers;
};

} // namespace AIAPI
