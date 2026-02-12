#pragma once

#include <QString>
#include <QStringList>
#include <QPointF>
#include <QRectF>
#include <QVector>
#include <QRegularExpression>

namespace acutesim::compute::orchestration {

/**
 * @brief A single placed component from a DEF COMPONENTS section.
 */
struct DEFPlacement {
    QString instanceName;
    QString cellName;
    QPointF defPosition;    // In DEF database units
    QString orientation;    // N, S, E, W, FN, FS, FE, FW
};

/**
 * @brief Parsed DEF design data (subset: header + placements).
 */
struct DEFDesign {
    QString designName;
    QRectF dieArea;                   // In DEF database units
    int unitsPerMicron = 1000;
    QVector<DEFPlacement> placements;
};

/**
 * @brief Lightweight DEF parser for extracting placement data.
 *
 * Parses: VERSION, DESIGN, UNITS, DIEAREA, COMPONENTS section.
 * This is the reverse of DEFTranslator — it reads DEF and produces
 * canvas-compatible placement data.
 */
class DEFParser {
public:
    /**
     * Parse a DEF file content string into a DEFDesign.
     */
    static DEFDesign parse(const QString& defContent) {
        DEFDesign design;
        QStringList lines = defContent.split('\n');

        bool inComponents = false;
        DEFPlacement currentPlacement;

        for (const auto& rawLine : lines) {
            QString line = rawLine.trimmed();
            if (line.isEmpty() || line.startsWith('#')) continue;

            // DESIGN name ;
            if (line.startsWith("DESIGN ")) {
                design.designName = line.section(' ', 1, 1).remove(';').trimmed();
                continue;
            }

            // UNITS DISTANCE MICRONS <n> ;
            if (line.startsWith("UNITS DISTANCE MICRONS")) {
                static QRegularExpression rx(R"(UNITS\s+DISTANCE\s+MICRONS\s+(\d+))");
                auto match = rx.match(line);
                if (match.hasMatch())
                    design.unitsPerMicron = match.captured(1).toInt();
                continue;
            }

            // DIEAREA ( x1 y1 ) ( x2 y2 ) ;
            if (line.startsWith("DIEAREA")) {
                static QRegularExpression rx(
                    R"(DIEAREA\s*\(\s*(-?\d+)\s+(-?\d+)\s*\)\s*\(\s*(-?\d+)\s+(-?\d+)\s*\))");
                auto match = rx.match(line);
                if (match.hasMatch()) {
                    double x1 = match.captured(1).toDouble();
                    double y1 = match.captured(2).toDouble();
                    double x2 = match.captured(3).toDouble();
                    double y2 = match.captured(4).toDouble();
                    design.dieArea = QRectF(QPointF(x1, y1), QPointF(x2, y2));
                }
                continue;
            }

            // COMPONENTS section
            if (line.startsWith("COMPONENTS ")) {
                inComponents = true;
                continue;
            }
            if (line == "END COMPONENTS") {
                inComponents = false;
                continue;
            }

            if (inComponents && line.startsWith("- ")) {
                // - <instanceName> <cellName> + PLACED ( x y ) <orient> ;
                static QRegularExpression rx(
                    R"(-\s+(\S+)\s+(\S+)\s+.*\(\s*(-?\d+)\s+(-?\d+)\s*\)\s*(\S+))");
                auto match = rx.match(line);
                if (match.hasMatch()) {
                    DEFPlacement p;
                    p.instanceName = match.captured(1);
                    p.cellName = match.captured(2);
                    p.defPosition = QPointF(match.captured(3).toDouble(),
                                            match.captured(4).toDouble());
                    p.orientation = match.captured(5).remove(';');
                    design.placements.append(p);
                }
            }
        }

        return design;
    }

    /**
     * Convert DEF coordinates back to canvas coordinates.
     * Reverses the canvasToDef transform from DEFTranslator.
     *
     * canvasX = defX / (pixelToMicronScale * defUnitsPerMicron)
     * canvasY = canvasHeight - defY / (pixelToMicronScale * defUnitsPerMicron)
     */
    static QPointF defToCanvas(const QPointF& defPos,
                                double canvasHeight,
                                double pixelToMicronScale,
                                int defUnitsPerMicron) {
        double scale = pixelToMicronScale * defUnitsPerMicron;
        double canvasX = defPos.x() / scale;
        double canvasY = canvasHeight - defPos.y() / scale;
        return QPointF(canvasX, canvasY);
    }
};

} // namespace acutesim::compute::orchestration
