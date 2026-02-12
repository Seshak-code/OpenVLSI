#include "openroad_report_parser.h"
#include <QRegularExpression>
#include <QTextStream>
#include <QtMath>

namespace acutesim::compute::orchestration {

// ============================================================================
// PLACEMENT / DESIGN AREA PARSER
// ============================================================================
//
// OpenROAD `report_design_area` output format:
//   Design area 12345.678 u^2 67% utilization.
//
// Or (newer versions):
//   Design area: 12345.678
//   Utilization: 67.00%
//   Number of instances: 1234

PlacementResult OpenRoadReportParser::parseDesignArea(const QString& output) {
    PlacementResult result;
    
    // Pattern 1: "Design area 12345.678 u^2 67% utilization."
    static QRegularExpression areaRe1(
        R"(Design area\s+([\d.]+)\s+u\^2\s+([\d.]+)%\s+utilization)");
    auto match1 = areaRe1.match(output);
    if (match1.hasMatch()) {
        result.totalArea = match1.captured(1).toDouble();
        result.utilization = match1.captured(2).toDouble() / 100.0;
    }
    
    // Pattern 2: Separate lines
    static QRegularExpression areaRe2(R"(Design area:\s*([\d.]+))");
    static QRegularExpression utilRe(R"(Utilization:\s*([\d.]+)%)");
    static QRegularExpression instRe(R"(Number of instances:\s*(\d+))");
    
    auto match2 = areaRe2.match(output);
    if (match2.hasMatch()) result.totalArea = match2.captured(1).toDouble();
    
    auto matchUtil = utilRe.match(output);
    if (matchUtil.hasMatch()) result.utilization = matchUtil.captured(1).toDouble() / 100.0;
    
    auto matchInst = instRe.match(output);
    if (matchInst.hasMatch()) result.instanceCount = matchInst.captured(1).toInt();
    
    return result;
}

// ============================================================================
// TIMING PARSER
// ============================================================================
//
// OpenROAD `report_worst_slack -max` output format:
//   worst slack -0.123
//   tns -4.567
//   Failing endpoints: 3

TimingResult OpenRoadReportParser::parseTiming(const QString& output) {
    TimingResult result;
    
    static QRegularExpression wnsRe(R"(worst slack\s+(-?[\d.]+))");
    static QRegularExpression tnsRe(R"(tns\s+(-?[\d.]+))");
    static QRegularExpression failRe(R"(Failing endpoints:\s*(\d+))");
    static QRegularExpression setupRe(R"(Setup violations:\s*(\d+))");
    static QRegularExpression holdRe(R"(Hold violations:\s*(\d+))");
    
    auto wnsMatch = wnsRe.match(output);
    if (wnsMatch.hasMatch()) result.wns = wnsMatch.captured(1).toDouble();
    
    auto tnsMatch = tnsRe.match(output);
    if (tnsMatch.hasMatch()) result.tns = tnsMatch.captured(1).toDouble();
    
    auto failMatch = failRe.match(output);
    if (failMatch.hasMatch()) result.setupViolations = failMatch.captured(1).toInt();
    
    auto setupMatch = setupRe.match(output);
    if (setupMatch.hasMatch()) result.setupViolations = setupMatch.captured(1).toInt();
    
    auto holdMatch = holdRe.match(output);
    if (holdMatch.hasMatch()) result.holdViolations = holdMatch.captured(1).toInt();
    
    // Derive clean flag
    result.timingClean = (result.setupViolations == 0 && result.holdViolations == 0 && result.wns >= 0.0);
    
    return result;
}

// ============================================================================
// POWER PARSER
// ============================================================================
//
// OpenROAD `report_power` output format:
//   Total Power     = 1.234 mW
//   Dynamic Power   = 0.567 mW
//   Leakage Power   = 0.012 mW
//   Internal Power  = 0.345 mW

PowerResult OpenRoadReportParser::parsePower(const QString& output) {
    PowerResult result;
    
    static QRegularExpression totalRe(R"(Total\s+(?:Power\s*)?=?\s*([\d.eE+-]+)\s*(?:mW|W))");
    static QRegularExpression dynRe(R"(Dynamic\s+(?:Power\s*)?=?\s*([\d.eE+-]+)\s*(?:mW|W))");
    static QRegularExpression leakRe(R"(Leakage\s+(?:Power\s*)?=?\s*([\d.eE+-]+)\s*(?:mW|W))");
    static QRegularExpression intRe(R"(Internal\s+(?:Power\s*)?=?\s*([\d.eE+-]+)\s*(?:mW|W))");
    
    auto totalMatch = totalRe.match(output);
    if (totalMatch.hasMatch()) result.totalPower = totalMatch.captured(1).toDouble();
    
    auto dynMatch = dynRe.match(output);
    if (dynMatch.hasMatch()) result.dynamicPower = dynMatch.captured(1).toDouble();
    
    auto leakMatch = leakRe.match(output);
    if (leakMatch.hasMatch()) result.leakagePower = leakMatch.captured(1).toDouble();
    
    auto intMatch = intRe.match(output);
    if (intMatch.hasMatch()) result.internalPower = intMatch.captured(1).toDouble();
    
    return result;
}

// ============================================================================
// ROUTING PARSER
// ============================================================================
//
// OpenROAD `check_routing` output format:
//   Number of routed nets: 1234
//   Number of unrouted nets: 0
//   DRC violations: 0
//   Antenna violations: 0

RoutingResult OpenRoadReportParser::parseRouting(const QString& output) {
    RoutingResult result;
    
    static QRegularExpression routedRe(R"((?:Number of )?[Rr]outed nets:\s*(\d+))");
    static QRegularExpression unroutedRe(R"((?:Number of )?[Uu]nrouted nets:\s*(\d+))");
    static QRegularExpression drcRe(R"(DRC violations:\s*(\d+))");
    static QRegularExpression antRe(R"([Aa]ntenna violations:\s*(\d+))");
    
    auto routedMatch = routedRe.match(output);
    if (routedMatch.hasMatch()) result.routedNets = routedMatch.captured(1).toInt();
    
    auto unroutedMatch = unroutedRe.match(output);
    if (unroutedMatch.hasMatch()) result.unroutedNets = unroutedMatch.captured(1).toInt();
    
    auto drcMatch = drcRe.match(output);
    if (drcMatch.hasMatch()) result.drcViolations = drcMatch.captured(1).toInt();
    
    auto antMatch = antRe.match(output);
    if (antMatch.hasMatch()) result.antennaViolations = antMatch.captured(1).toInt();
    
    // Derive clean flag
    result.drcClean = (result.drcViolations == 0);
    
    return result;
}

// ============================================================================
// FULL OUTPUT PARSER
// ============================================================================

PnRResult OpenRoadReportParser::parse(const QString& output) {
    PnRResult result;
    result.rawOutput = output;
    
    // Parse all sections
    result.placement = parseDesignArea(output);
    result.timing = parseTiming(output);
    result.power = parsePower(output);
    result.routing = parseRouting(output);
    
    // Extract warnings
    QStringList lines = output.split('\n');
    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.startsWith("[WARNING]") || trimmed.startsWith("Warning:")) {
            result.warnings.append(trimmed);
        }
    }
    
    // Parse errors
    parseErrors(output, result);
    
    // Success: no errors found
    result.success = result.errors.isEmpty();
    
    return result;
}

// ============================================================================
// ERROR PARSER
// ============================================================================

void OpenRoadReportParser::parseErrors(const QString& stderr, PnRResult& result) {
    QStringList lines = stderr.split('\n');
    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.startsWith("[ERROR]") || trimmed.startsWith("Error:")) {
            result.errors.append(trimmed);
        }
    }
}

// ============================================================================
// DEF TRANSLATOR
// ============================================================================

QPointF DEFTranslator::canvasToDef(const QPointF& canvasPos,
                                    double canvasHeight,
                                    double pixelToMicronScale,
                                    int defUnitsPerMicron) {
    // Canvas: upper-left origin, Y increases downward
    // DEF:    lower-left origin, Y increases upward
    double defX = canvasPos.x() * pixelToMicronScale * defUnitsPerMicron;
    double defY = (canvasHeight - canvasPos.y()) * pixelToMicronScale * defUnitsPerMicron;
    return QPointF(defX, defY);
}

QString DEFTranslator::toDefOrientation(double rotationDegrees, bool mirrored) {
    // Normalize to [0, 360)
    int rot = static_cast<int>(std::fmod(rotationDegrees, 360.0));
    if (rot < 0) rot += 360;
    
    if (!mirrored) {
        switch (rot) {
            case 0:   return "N";   // Normal
            case 90:  return "E";   // 90° CW
            case 180: return "S";   // 180°
            case 270: return "W";   // 270° CW
            default:  return "N";
        }
    } else {
        // Flipped (mirrored about Y-axis, then rotated)
        switch (rot) {
            case 0:   return "FN";  // Flipped Normal
            case 90:  return "FE";  // Flipped + 90°
            case 180: return "FS";  // Flipped + 180°
            case 270: return "FW";  // Flipped + 270°
            default:  return "FN";
        }
    }
}

QString DEFTranslator::generateDEF(
    const QString& designName,
    const QList<SchematicPlacement>& placements,
    const QSizeF& dieArea,
    double pixelToMicronScale,
    double canvasHeight,
    int defUnitsPerMicron) {
    
    QString def;
    QTextStream out(&def);
    
    // DEF Header
    out << "VERSION 5.8 ;\n";
    out << "DIVIDERCHAR \"/\" ;\n";
    out << "BUSBITCHARS \"[]\" ;\n";
    out << "DESIGN " << designName << " ;\n";
    out << "UNITS DISTANCE MICRONS " << defUnitsPerMicron << " ;\n";
    out << "\n";
    
    // Die Area (already in micrometers, convert to DEF units)
    int dieW = static_cast<int>(dieArea.width() * defUnitsPerMicron);
    int dieH = static_cast<int>(dieArea.height() * defUnitsPerMicron);
    out << "DIEAREA ( 0 0 ) ( " << dieW << " " << dieH << " ) ;\n";
    out << "\n";
    
    // Components Section
    out << "COMPONENTS " << placements.size() << " ;\n";
    
    for (const SchematicPlacement& p : placements) {
        QPointF defPos = canvasToDef(p.canvasPosition, canvasHeight,
                                      pixelToMicronScale, defUnitsPerMicron);
        QString orient = toDefOrientation(p.rotation, p.mirrored);
        
        out << "    - " << p.instanceName
            << " " << p.cellName
            << " + PLACED ( "
            << static_cast<int>(defPos.x()) << " "
            << static_cast<int>(defPos.y())
            << " ) " << orient << " ;\n";
    }
    
    out << "END COMPONENTS\n";
    out << "\n";
    out << "END DESIGN\n";
    
    return def;
}

} // namespace acutesim::compute::orchestration
