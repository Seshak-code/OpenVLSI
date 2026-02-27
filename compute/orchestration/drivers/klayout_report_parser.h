#pragma once

#include <string>
#include <vector>
#include "eda_types.h"

namespace acutesim::compute::orchestration {

// ============================================================================
// DRC RESULT STRUCTURES
// ============================================================================

struct DRCViolation {
    std::string ruleName;
    std::string category;
    std::string description;
    std::string layer;
    std::string severity;
    Rect2D      bbox;
    int         count = 1;
};

struct DRCResult {
    bool success = false;
    bool cleanDRC = false;
    int  totalViolations = 0;
    int  errorCount = 0;
    int  warningCount = 0;
    std::vector<DRCViolation> violations;
    std::vector<std::string>  errors;
    std::string rawOutput;
};

// ============================================================================
// LVS RESULT STRUCTURES
// ============================================================================

struct LVSMismatch {
    std::string type;
    std::string layoutName;
    std::string schematicName;
    std::string details;
};

struct LVSResult {
    bool success = false;
    bool matched = false;
    int  deviceCountLayout = 0;
    int  deviceCountSchematic = 0;
    int  netCountLayout = 0;
    int  netCountSchematic = 0;
    int  mismatchCount = 0;
    std::vector<LVSMismatch> mismatches;
    std::vector<std::string> errors;
    std::string rawOutput;
};

// ============================================================================
// PARSER
// ============================================================================

class KLayoutReportParser {
public:
    static DRCResult parseDRC(const std::string& output);
    static LVSResult parseLVS(const std::string& output);
    static void parseDRCErrors(const std::string& stderrOutput, DRCResult& result);
    static void parseLVSErrors(const std::string& stderrOutput, LVSResult& result);
};

} // namespace acutesim::compute::orchestration
