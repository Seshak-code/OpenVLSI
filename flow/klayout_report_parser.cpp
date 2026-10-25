#include "klayout_report_parser.h"
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

bool containsCI(const std::string& haystack, const std::string& needle) {
    // Case-insensitive substring search
    auto it = std::search(haystack.begin(), haystack.end(),
                          needle.begin(), needle.end(),
                          [](char a, char b) { return std::tolower(a) == std::tolower(b); });
    return it != haystack.end();
}

} // anonymous namespace

// ============================================================================
// DRC PARSER
// ============================================================================

DRCResult KLayoutReportParser::parseDRC(const std::string& output) {
    DRCResult result;
    result.rawOutput = output;
    result.success = true;

    if (trimStr(output).empty()) {
        result.success = false;
        result.errors.push_back("Empty DRC output");
        return result;
    }

    // Rule: metal1.width | Category: Width | Layer: metal1 | Violations: 3
    static const std::regex violationRe(
        R"(Rule:\s*(\S+)\s*\|\s*Category:\s*([^|]+)\|\s*Layer:\s*(\S+)\s*\|\s*Violations:\s*(\d+))",
        std::regex::icase);

    auto begin = std::sregex_iterator(output.begin(), output.end(), violationRe);
    auto end   = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        const std::smatch& match = *it;
        DRCViolation v;
        v.ruleName = trimStr(match[1].str());
        v.category = trimStr(match[2].str());
        v.layer    = trimStr(match[3].str());
        v.count    = std::stoi(match[4].str());
        v.severity = "error";
        v.description = v.category + " violation on " + v.layer +
                        " (" + std::to_string(v.count) + " instances)";
        result.violations.push_back(v);
        result.totalViolations += v.count;
        result.errorCount += v.count;
    }

    // Total DRC violations: N
    static const std::regex totalRe(R"(Total\s+DRC\s+violations:\s*(\d+))", std::regex::icase);
    std::smatch totalMatch;
    if (std::regex_search(output, totalMatch, totalRe))
        result.totalViolations = std::stoi(totalMatch[1].str());

    if (containsCI(output, "DRC clean") ||
        containsCI(output, "No DRC violations") ||
        result.totalViolations == 0) {
        result.cleanDRC = true;
    }

    // Warning lines
    static const std::regex warningRe(R"(Warning:\s*(.+))", std::regex::icase);
    auto wb = std::sregex_iterator(output.begin(), output.end(), warningRe);
    for (auto it = wb; it != end; ++it) {
        const std::smatch& match = *it;
        DRCViolation v;
        v.severity    = "warning";
        v.description = trimStr(match[1].str());
        v.ruleName    = "warning";
        v.count       = 1;
        result.violations.push_back(v);
        result.warningCount++;
    }

    return result;
}

// ============================================================================
// LVS PARSER
// ============================================================================

LVSResult KLayoutReportParser::parseLVS(const std::string& output) {
    LVSResult result;
    result.rawOutput = output;
    result.success = true;

    if (trimStr(output).empty()) {
        result.success = false;
        result.errors.push_back("Empty LVS output");
        return result;
    }

    if (containsCI(output, "MATCH") && !containsCI(output, "MISMATCH"))
        result.matched = true;
    if (containsCI(output, "MISMATCH") || containsCI(output, "NOT MATCH"))
        result.matched = false;

    // Layout devices: 42 | Schematic devices: 42
    static const std::regex deviceRe(
        R"(Layout\s+devices:\s*(\d+)\s*\|\s*Schematic\s+devices:\s*(\d+))", std::regex::icase);
    std::smatch dm;
    if (std::regex_search(output, dm, deviceRe)) {
        result.deviceCountLayout    = std::stoi(dm[1].str());
        result.deviceCountSchematic = std::stoi(dm[2].str());
    }

    // Layout nets: 28 | Schematic nets: 28
    static const std::regex netRe(
        R"(Layout\s+nets:\s*(\d+)\s*\|\s*Schematic\s+nets:\s*(\d+))", std::regex::icase);
    std::smatch nm;
    if (std::regex_search(output, nm, netRe)) {
        result.netCountLayout    = std::stoi(nm[1].str());
        result.netCountSchematic = std::stoi(nm[2].str());
    }

    // Mismatch: net | Layout: NET_A | Schematic: net_a | Extra net in layout
    static const std::regex mismatchRe(
        R"(Mismatch:\s*(\w+)\s*\|\s*Layout:\s*([^|]+)\|\s*Schematic:\s*([^|]+)\|\s*(.+))",
        std::regex::icase);

    auto begin = std::sregex_iterator(output.begin(), output.end(), mismatchRe);
    auto end   = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        const std::smatch& match = *it;
        LVSMismatch mm;
        mm.type           = trimStr(match[1].str());
        mm.layoutName     = trimStr(match[2].str());
        mm.schematicName  = trimStr(match[3].str());
        mm.details        = trimStr(match[4].str());
        result.mismatches.push_back(mm);
    }
    result.mismatchCount = static_cast<int>(result.mismatches.size());

    // Total mismatches: N
    static const std::regex totalMmRe(R"(Total\s+mismatches:\s*(\d+))", std::regex::icase);
    std::smatch tmm;
    if (std::regex_search(output, tmm, totalMmRe))
        result.mismatchCount = std::stoi(tmm[1].str());

    return result;
}

// ============================================================================
// ERROR PARSING
// ============================================================================

void KLayoutReportParser::parseDRCErrors(const std::string& stderrOutput, DRCResult& result) {
    if (trimStr(stderrOutput).empty()) return;
    for (const auto& line : splitLines(stderrOutput)) {
        std::string t = trimStr(line);
        if (t.empty()) continue;
        if (containsCI(t, "error") || containsCI(t, "fatal") || containsCI(t, "exception"))
            result.errors.push_back(t);
    }
}

void KLayoutReportParser::parseLVSErrors(const std::string& stderrOutput, LVSResult& result) {
    if (trimStr(stderrOutput).empty()) return;
    for (const auto& line : splitLines(stderrOutput)) {
        std::string t = trimStr(line);
        if (t.empty()) continue;
        if (containsCI(t, "error") || containsCI(t, "fatal") || containsCI(t, "exception"))
            result.errors.push_back(t);
    }
}

} // namespace acutesim::compute::orchestration
