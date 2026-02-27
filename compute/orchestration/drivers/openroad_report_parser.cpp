#include "openroad_report_parser.h"
#include <regex>
#include <sstream>
#include <cmath>

namespace acutesim::compute::orchestration {

namespace {

std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> result;
    std::istringstream iss(s);
    std::string line;
    while (std::getline(iss, line)) result.push_back(line);
    return result;
}

std::string trimStr(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end   = s.find_last_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    return s.substr(start, end - start + 1);
}

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

} // anonymous namespace

// ============================================================================
// PLACEMENT PARSER
// ============================================================================

PlacementResult OpenRoadReportParser::parseDesignArea(const std::string& output) {
    PlacementResult result;

    // Pattern 1: "Design area 12345.678 u^2 67% utilization."
    static const std::regex areaRe1(R"(Design area\s+([\d.]+)\s+u\^2\s+([\d.]+)%\s+utilization)");
    std::smatch m1;
    if (std::regex_search(output, m1, areaRe1)) {
        result.totalArea    = std::stod(m1[1].str());
        result.utilization  = std::stod(m1[2].str()) / 100.0;
    }

    // Pattern 2: Separate lines
    static const std::regex areaRe2(R"(Design area:\s*([\d.]+))");
    static const std::regex utilRe(R"(Utilization:\s*([\d.]+)%)");
    static const std::regex instRe(R"(Number of instances:\s*(\d+))");

    std::smatch m2;
    if (std::regex_search(output, m2, areaRe2)) result.totalArea   = std::stod(m2[1].str());
    std::smatch mu;
    if (std::regex_search(output, mu, utilRe))  result.utilization = std::stod(mu[1].str()) / 100.0;
    std::smatch mi;
    if (std::regex_search(output, mi, instRe))  result.instanceCount = std::stoi(mi[1].str());

    return result;
}

// ============================================================================
// TIMING PARSER
// ============================================================================

TimingResult OpenRoadReportParser::parseTiming(const std::string& output) {
    TimingResult result;

    static const std::regex wnsRe(R"(worst slack\s+(-?[\d.]+))");
    static const std::regex tnsRe(R"(tns\s+(-?[\d.]+))");
    static const std::regex failRe(R"(Failing endpoints:\s*(\d+))");
    static const std::regex setupRe(R"(Setup violations:\s*(\d+))");
    static const std::regex holdRe(R"(Hold violations:\s*(\d+))");

    std::smatch m;
    if (std::regex_search(output, m, wnsRe))   result.wns = std::stod(m[1].str());
    if (std::regex_search(output, m, tnsRe))   result.tns = std::stod(m[1].str());
    if (std::regex_search(output, m, failRe))  result.setupViolations = std::stoi(m[1].str());
    if (std::regex_search(output, m, setupRe)) result.setupViolations = std::stoi(m[1].str());
    if (std::regex_search(output, m, holdRe))  result.holdViolations  = std::stoi(m[1].str());

    result.timingClean = (result.setupViolations == 0 && result.holdViolations == 0 && result.wns >= 0.0);
    return result;
}

// ============================================================================
// POWER PARSER
// ============================================================================

PowerResult OpenRoadReportParser::parsePower(const std::string& output) {
    PowerResult result;

    static const std::regex totalRe(R"(Total\s+(?:Power\s*)?=?\s*([\d.eE+-]+)\s*(?:mW|W))");
    static const std::regex dynRe(R"(Dynamic\s+(?:Power\s*)?=?\s*([\d.eE+-]+)\s*(?:mW|W))");
    static const std::regex leakRe(R"(Leakage\s+(?:Power\s*)?=?\s*([\d.eE+-]+)\s*(?:mW|W))");
    static const std::regex intRe(R"(Internal\s+(?:Power\s*)?=?\s*([\d.eE+-]+)\s*(?:mW|W))");

    std::smatch m;
    if (std::regex_search(output, m, totalRe)) result.totalPower    = std::stod(m[1].str());
    if (std::regex_search(output, m, dynRe))   result.dynamicPower  = std::stod(m[1].str());
    if (std::regex_search(output, m, leakRe))  result.leakagePower  = std::stod(m[1].str());
    if (std::regex_search(output, m, intRe))   result.internalPower = std::stod(m[1].str());

    return result;
}

// ============================================================================
// ROUTING PARSER
// ============================================================================

RoutingResult OpenRoadReportParser::parseRouting(const std::string& output) {
    RoutingResult result;

    static const std::regex routedRe(R"((?:Number of )?[Rr]outed nets:\s*(\d+))");
    static const std::regex unroutedRe(R"((?:Number of )?[Uu]nrouted nets:\s*(\d+))");
    static const std::regex drcRe(R"(DRC violations:\s*(\d+))");
    static const std::regex antRe(R"([Aa]ntenna violations:\s*(\d+))");

    std::smatch m;
    if (std::regex_search(output, m, routedRe))   result.routedNets        = std::stoi(m[1].str());
    if (std::regex_search(output, m, unroutedRe)) result.unroutedNets      = std::stoi(m[1].str());
    if (std::regex_search(output, m, drcRe))      result.drcViolations     = std::stoi(m[1].str());
    if (std::regex_search(output, m, antRe))      result.antennaViolations = std::stoi(m[1].str());

    result.drcClean = (result.drcViolations == 0);
    return result;
}

// ============================================================================
// FULL OUTPUT PARSER
// ============================================================================

PnRResult OpenRoadReportParser::parse(const std::string& output) {
    PnRResult result;
    result.rawOutput = output;

    result.placement = parseDesignArea(output);
    result.timing    = parseTiming(output);
    result.power     = parsePower(output);
    result.routing   = parseRouting(output);

    for (const auto& line : splitLines(output)) {
        std::string t = trimStr(line);
        if (startsWith(t, "[WARNING]") || startsWith(t, "Warning:"))
            result.warnings.push_back(t);
    }

    parseErrors(output, result);
    result.success = result.errors.empty();
    return result;
}

// ============================================================================
// ERROR PARSER
// ============================================================================

void OpenRoadReportParser::parseErrors(const std::string& stderrOutput, PnRResult& result) {
    for (const auto& line : splitLines(stderrOutput)) {
        std::string t = trimStr(line);
        if (startsWith(t, "[ERROR]") || startsWith(t, "Error:"))
            result.errors.push_back(t);
    }
}

// ============================================================================
// DEF TRANSLATOR
// ============================================================================

Point2D DEFTranslator::canvasToDef(const Point2D& canvasPos,
                                    double canvasHeight,
                                    double pixelToMicronScale,
                                    int defUnitsPerMicron) {
    double scale = pixelToMicronScale * defUnitsPerMicron;
    return { canvasPos.x * scale,
             (canvasHeight - canvasPos.y) * scale };
}

std::string DEFTranslator::toDefOrientation(double rotationDegrees, bool mirrored) {
    int rot = static_cast<int>(std::fmod(rotationDegrees, 360.0));
    if (rot < 0) rot += 360;

    if (!mirrored) {
        switch (rot) {
            case 0:   return "N";
            case 90:  return "E";
            case 180: return "S";
            case 270: return "W";
            default:  return "N";
        }
    } else {
        switch (rot) {
            case 0:   return "FN";
            case 90:  return "FE";
            case 180: return "FS";
            case 270: return "FW";
            default:  return "FN";
        }
    }
}

std::string DEFTranslator::generateDEF(
    const std::string& designName,
    const std::vector<SchematicPlacement>& placements,
    const Size2D& dieArea,
    double pixelToMicronScale,
    double canvasHeight,
    int defUnitsPerMicron) {

    std::ostringstream out;

    out << "VERSION 5.8 ;\n"
        << "DIVIDERCHAR \"/\" ;\n"
        << "BUSBITCHARS \"[]\" ;\n"
        << "DESIGN " << designName << " ;\n"
        << "UNITS DISTANCE MICRONS " << defUnitsPerMicron << " ;\n\n";

    int dieW = static_cast<int>(dieArea.width  * defUnitsPerMicron);
    int dieH = static_cast<int>(dieArea.height * defUnitsPerMicron);
    out << "DIEAREA ( 0 0 ) ( " << dieW << " " << dieH << " ) ;\n\n";

    out << "COMPONENTS " << placements.size() << " ;\n";
    for (const auto& p : placements) {
        Point2D defPos = canvasToDef(p.canvasPosition, canvasHeight,
                                      pixelToMicronScale, defUnitsPerMicron);
        std::string orient = toDefOrientation(p.rotation, p.mirrored);

        out << "    - " << p.instanceName << " " << p.cellName
            << " + PLACED ( " << static_cast<int>(defPos.x)
            << " " << static_cast<int>(defPos.y) << " ) "
            << orient << " ;\n";
    }

    out << "END COMPONENTS\n\nEND DESIGN\n";
    return out.str();
}

} // namespace acutesim::compute::orchestration
