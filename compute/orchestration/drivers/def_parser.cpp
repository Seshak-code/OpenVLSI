#include "def_parser.h"
#include <regex>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace acutesim::compute::orchestration {

namespace {

std::string trimStr(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end   = s.find_last_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    return s.substr(start, end - start + 1);
}

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

// Remove all occurrences of ch from string
std::string removeChar(std::string s, char ch) {
    s.erase(std::remove(s.begin(), s.end(), ch), s.end());
    return s;
}

// Split string by whitespace into tokens
std::vector<std::string> tokenize(const std::string& s) {
    std::vector<std::string> tokens;
    std::istringstream iss(s);
    std::string tok;
    while (iss >> tok) tokens.push_back(tok);
    return tokens;
}

} // anonymous namespace

DEFDesign DEFParser::parse(const std::string& defContent) {
    DEFDesign design;
    std::istringstream stream(defContent);
    std::string rawLine;

    bool inComponents = false;

    // Pre-compiled regexes (static for performance)
    static const std::regex unitsRe(R"(UNITS\s+DISTANCE\s+MICRONS\s+(\d+))");
    static const std::regex dieAreaRe(
        R"(DIEAREA\s*\(\s*(-?\d+)\s+(-?\d+)\s*\)\s*\(\s*(-?\d+)\s+(-?\d+)\s*\))");
    static const std::regex compRe(
        R"(-\s+(\S+)\s+(\S+)\s+.*\(\s*(-?\d+)\s+(-?\d+)\s*\)\s*(\S+))");

    while (std::getline(stream, rawLine)) {
        std::string line = trimStr(rawLine);
        if (line.empty() || line[0] == '#') continue;

        // DESIGN name ;
        if (startsWith(line, "DESIGN ")) {
            auto tokens = tokenize(line);
            if (tokens.size() >= 2)
                design.designName = removeChar(tokens[1], ';');
            continue;
        }

        // UNITS DISTANCE MICRONS <n> ;
        if (startsWith(line, "UNITS DISTANCE MICRONS")) {
            std::smatch m;
            if (std::regex_search(line, m, unitsRe))
                design.unitsPerMicron = std::stoi(m[1].str());
            continue;
        }

        // DIEAREA ( x1 y1 ) ( x2 y2 ) ;
        if (startsWith(line, "DIEAREA")) {
            std::smatch m;
            if (std::regex_search(line, m, dieAreaRe)) {
                double x1 = std::stod(m[1].str());
                double y1 = std::stod(m[2].str());
                double x2 = std::stod(m[3].str());
                double y2 = std::stod(m[4].str());
                design.dieArea = { x1, y1, x2 - x1, y2 - y1 };
            }
            continue;
        }

        // COMPONENTS section
        if (startsWith(line, "COMPONENTS ")) {
            inComponents = true;
            continue;
        }
        if (line == "END COMPONENTS") {
            inComponents = false;
            continue;
        }

        if (inComponents && startsWith(line, "- ")) {
            std::smatch m;
            if (std::regex_search(line, m, compRe)) {
                DEFPlacement p;
                p.instanceName = m[1].str();
                p.cellName     = m[2].str();
                p.defPosition  = { std::stod(m[3].str()), std::stod(m[4].str()) };
                p.orientation  = removeChar(m[5].str(), ';');
                design.placements.push_back(p);
            }
        }
    }

    return design;
}

Point2D DEFParser::defToCanvas(const Point2D& defPos,
                                double canvasHeight,
                                double pixelToMicronScale,
                                int defUnitsPerMicron) {
    double scale = pixelToMicronScale * defUnitsPerMicron;
    return { defPos.x / scale,
             canvasHeight - defPos.y / scale };
}

} // namespace acutesim::compute::orchestration
