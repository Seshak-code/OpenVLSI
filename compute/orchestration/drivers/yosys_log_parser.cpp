#include "yosys_log_parser.h"
#include <regex>
#include <sstream>

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
// MODULE BLOCK PARSER
// ============================================================================

ModuleStats YosysLogParser::parseModuleBlock(const std::vector<std::string>& lines,
                                              int startLine, int& endLine) {
    ModuleStats stats;

    static const std::regex moduleRe(R"(^=== (\S+) ===$)");
    std::smatch m;
    std::string trimmedStart = trimStr(lines[startLine]);
    if (std::regex_match(trimmedStart, m, moduleRe))
        stats.moduleName = m[1].str();

    static const std::regex statRe(R"(^\s+Number of (\w[\w ]*\w)\s*:\s+(\d+))");
    static const std::regex cellRe(R"(^\s{4,}(\S+)\s+(\d+))");
    static const std::regex areaRe(R"(Chip area for module '\\?(\S+)':\s+([\d.]+))");

    bool inCellList = false;

    for (int i = startLine + 1; i < static_cast<int>(lines.size()); ++i) {
        const std::string& line = lines[i];
        std::string trimmed = trimStr(line);

        // Next module block
        if (trimmed.size() >= 7 && startsWith(trimmed, "===") &&
            trimmed.compare(trimmed.size() - 3, 3, "===") == 0 && i > startLine + 1) {
            endLine = i - 1;
            return stats;
        }

        std::smatch sm;
        if (std::regex_search(line, sm, statRe)) {
            std::string key = trimStr(sm[1].str());
            int value = std::stoi(sm[2].str());

            if      (key == "wires")         stats.wireCount = value;
            else if (key == "wire bits")     stats.wireBits = value;
            else if (key == "public wires")  stats.publicWires = value;
            else if (key == "cells")         { stats.cellCount = value; inCellList = true; }
            else if (key == "memories")      stats.memoryBits = value;
            else if (key == "processes")     stats.processes = value;

            if (key != "cells") inCellList = false;
            continue;
        }

        if (inCellList) {
            std::smatch cm;
            if (std::regex_search(line, cm, cellRe)) {
                stats.cellBreakdown[cm[1].str()] = std::stoi(cm[2].str());
                continue;
            } else if (!trimmed.empty()) {
                inCellList = false;
            }
        }

        std::smatch am;
        if (std::regex_search(line, am, areaRe))
            stats.chipArea = std::stod(am[2].str());
    }

    endLine = static_cast<int>(lines.size()) - 1;
    return stats;
}

// ============================================================================
// MAIN PARSER
// ============================================================================

SynthesisResult YosysLogParser::parse(const std::string& output) {
    SynthesisResult result;
    result.rawOutput = output;

    std::vector<std::string> lines = splitLines(output);
    static const std::regex moduleHeaderRe(R"(^=== (\S+) ===$)");

    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        std::string trimmed = trimStr(lines[i]);
        std::smatch m;

        if (std::regex_match(trimmed, m, moduleHeaderRe)) {
            int endLine = i;
            ModuleStats modStats = parseModuleBlock(lines, i, endLine);
            result.modules[modStats.moduleName] = modStats;

            result.cellCount    = modStats.cellCount;
            result.wireCount    = modStats.wireCount;
            result.estimatedArea = modStats.chipArea;

            for (const auto& [k, v] : modStats.cellBreakdown)
                result.cellBreakdown[k] += v;

            i = endLine;
            continue;
        }

        if (startsWith(trimmed, "Warning:") || startsWith(trimmed, "warning:"))
            result.warnings.push_back(trimmed);
    }

    parseErrors(output, result);
    result.success = result.errors.empty();
    return result;
}

// ============================================================================
// ERROR PARSER
// ============================================================================

void YosysLogParser::parseErrors(const std::string& stderr, SynthesisResult& result) {
    for (const auto& line : splitLines(stderr)) {
        std::string trimmed = trimStr(line);
        if (startsWith(trimmed, "ERROR:") || startsWith(trimmed, "error:"))
            result.errors.push_back(trimmed);
    }
}

bool YosysLogParser::isSuccessful(const std::string& output) {
    return output.find("ERROR:") == std::string::npos;
}

} // namespace acutesim::compute::orchestration
