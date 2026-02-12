#include "klayout_report_parser.h"
#include <QRegularExpression>

namespace acutesim::compute::orchestration {

// ============================================================================
// DRC PARSER
// ============================================================================

DRCResult KLayoutReportParser::parseDRC(const QString& output) {
    DRCResult result;
    result.rawOutput = output;
    result.success = true;

    if (output.trimmed().isEmpty()) {
        result.success = false;
        result.errors.append("Empty DRC output");
        return result;
    }

    // Parse individual violations
    // KLayout DRC output format (marker DB summary):
    //   Rule: metal1.width | Category: Width | Layer: metal1 | Violations: 3
    //   Rule: via1.enclosure | Category: Enclosure | Layer: via1 | Violations: 1
    QRegularExpression violationRe(
        R"(Rule:\s*(\S+)\s*\|\s*Category:\s*([^|]+)\|\s*Layer:\s*(\S+)\s*\|\s*Violations:\s*(\d+))",
        QRegularExpression::CaseInsensitiveOption);
    
    auto it = violationRe.globalMatch(output);
    while (it.hasNext()) {
        auto match = it.next();
        DRCViolation v;
        v.ruleName = match.captured(1).trimmed();
        v.category = match.captured(2).trimmed();
        v.layer = match.captured(3).trimmed();
        v.count = match.captured(4).toInt();
        v.severity = "error"; // Default to error for DRC violations
        v.description = QString("%1 violation on %2 (%3 instances)")
            .arg(v.category, v.layer).arg(v.count);
        result.violations.append(v);
        result.totalViolations += v.count;
        result.errorCount += v.count;
    }

    // Parse summary line: "Total DRC violations: N"
    QRegularExpression totalRe(R"(Total\s+DRC\s+violations:\s*(\d+))",
        QRegularExpression::CaseInsensitiveOption);
    auto totalMatch = totalRe.match(output);
    if (totalMatch.hasMatch()) {
        result.totalViolations = totalMatch.captured(1).toInt();
    }

    // Parse "DRC clean" indicator
    if (output.contains("DRC clean", Qt::CaseInsensitive) ||
        output.contains("No DRC violations", Qt::CaseInsensitive) ||
        result.totalViolations == 0) {
        result.cleanDRC = true;
    }

    // Parse warning-level violations
    QRegularExpression warningRe(R"(Warning:\s*(.+))",
        QRegularExpression::CaseInsensitiveOption);
    auto warnIt = warningRe.globalMatch(output);
    while (warnIt.hasNext()) {
        auto match = warnIt.next();
        DRCViolation v;
        v.severity = "warning";
        v.description = match.captured(1).trimmed();
        v.ruleName = "warning";
        v.count = 1;
        result.violations.append(v);
        result.warningCount++;
    }

    return result;
}

// ============================================================================
// LVS PARSER
// ============================================================================

LVSResult KLayoutReportParser::parseLVS(const QString& output) {
    LVSResult result;
    result.rawOutput = output;
    result.success = true;

    if (output.trimmed().isEmpty()) {
        result.success = false;
        result.errors.append("Empty LVS output");
        return result;
    }

    // Parse match status
    if (output.contains("MATCH", Qt::CaseInsensitive) &&
        !output.contains("MISMATCH", Qt::CaseInsensitive)) {
        result.matched = true;
    }
    if (output.contains("MISMATCH", Qt::CaseInsensitive) ||
        output.contains("NOT MATCH", Qt::CaseInsensitive)) {
        result.matched = false;
    }

    // Parse device counts
    // "Layout devices: 42 | Schematic devices: 42"
    QRegularExpression deviceRe(
        R"(Layout\s+devices:\s*(\d+)\s*\|\s*Schematic\s+devices:\s*(\d+))",
        QRegularExpression::CaseInsensitiveOption);
    auto deviceMatch = deviceRe.match(output);
    if (deviceMatch.hasMatch()) {
        result.deviceCountLayout = deviceMatch.captured(1).toInt();
        result.deviceCountSchematic = deviceMatch.captured(2).toInt();
    }

    // Parse net counts
    // "Layout nets: 28 | Schematic nets: 28"
    QRegularExpression netRe(
        R"(Layout\s+nets:\s*(\d+)\s*\|\s*Schematic\s+nets:\s*(\d+))",
        QRegularExpression::CaseInsensitiveOption);
    auto netMatch = netRe.match(output);
    if (netMatch.hasMatch()) {
        result.netCountLayout = netMatch.captured(1).toInt();
        result.netCountSchematic = netMatch.captured(2).toInt();
    }

    // Parse mismatch details
    // "Mismatch: net | Layout: NET_A | Schematic: net_a | Extra net in layout"
    QRegularExpression mismatchRe(
        R"(Mismatch:\s*(\w+)\s*\|\s*Layout:\s*([^|]+)\|\s*Schematic:\s*([^|]+)\|\s*(.+))",
        QRegularExpression::CaseInsensitiveOption);
    auto mmIt = mismatchRe.globalMatch(output);
    while (mmIt.hasNext()) {
        auto match = mmIt.next();
        LVSMismatch mm;
        mm.type = match.captured(1).trimmed();
        mm.layoutName = match.captured(2).trimmed();
        mm.schematicName = match.captured(3).trimmed();
        mm.details = match.captured(4).trimmed();
        result.mismatches.append(mm);
    }
    result.mismatchCount = result.mismatches.size();

    // Parse total mismatch count if provided
    QRegularExpression totalMmRe(R"(Total\s+mismatches:\s*(\d+))",
        QRegularExpression::CaseInsensitiveOption);
    auto totalMmMatch = totalMmRe.match(output);
    if (totalMmMatch.hasMatch()) {
        result.mismatchCount = totalMmMatch.captured(1).toInt();
    }

    return result;
}

// ============================================================================
// ERROR PARSING
// ============================================================================

void KLayoutReportParser::parseDRCErrors(const QString& stderrOutput, DRCResult& result) {
    if (stderrOutput.trimmed().isEmpty()) return;

    for (const QString& line : stderrOutput.split('\n')) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;
        if (trimmed.contains("error", Qt::CaseInsensitive) ||
            trimmed.contains("fatal", Qt::CaseInsensitive) ||
            trimmed.contains("exception", Qt::CaseInsensitive)) {
            result.errors.append(trimmed);
        }
    }
}

void KLayoutReportParser::parseLVSErrors(const QString& stderrOutput, LVSResult& result) {
    if (stderrOutput.trimmed().isEmpty()) return;

    for (const QString& line : stderrOutput.split('\n')) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;
        if (trimmed.contains("error", Qt::CaseInsensitive) ||
            trimmed.contains("fatal", Qt::CaseInsensitive) ||
            trimmed.contains("exception", Qt::CaseInsensitive)) {
            result.errors.append(trimmed);
        }
    }
}

} // namespace acutesim::compute::orchestration
