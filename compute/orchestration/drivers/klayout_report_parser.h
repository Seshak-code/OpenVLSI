#pragma once

#include <QString>
#include <QStringList>
#include <QList>
#include <QRectF>

namespace acutesim::compute::orchestration {

// ============================================================================
// DRC RESULT STRUCTURES
// ============================================================================

/**
 * A single DRC violation reported by KLayout.
 */
struct DRCViolation {
    QString ruleName;       // e.g. "metal1.width"
    QString category;       // e.g. "Spacing", "Width", "Enclosure"
    QString description;    // Human-readable description
    QString layer;          // e.g. "metal1", "poly"
    QString severity;       // "error", "warning", "info"
    QRectF  bbox;           // Bounding box of violation (in layout coords)
    int     count = 1;      // Number of instances of this specific violation
};

/**
 * Aggregated DRC results from a KLayout run.
 */
struct DRCResult {
    bool success = false;           // Process completed without crashing
    bool cleanDRC = false;          // Zero violations at ERROR level
    int  totalViolations = 0;       // Sum of all violation counts
    int  errorCount = 0;            // ERROR-level violations
    int  warningCount = 0;          // WARNING-level violations
    QList<DRCViolation> violations; // Individual violations
    QStringList errors;             // Process-level errors (tool crash, timeout)
    QString rawOutput;              // Full stdout for debugging
};

// ============================================================================
// LVS RESULT STRUCTURES
// ============================================================================

/**
 * A single LVS mismatch entry.
 */
struct LVSMismatch {
    QString type;           // "net", "device", "pin", "parameter"
    QString layoutName;     // Name in layout extraction
    QString schematicName;  // Name in schematic netlist
    QString details;        // Detailed mismatch description
};

/**
 * Aggregated LVS results from a KLayout run.
 */
struct LVSResult {
    bool success = false;           // Process completed without crashing
    bool matched = false;           // Layout matches schematic
    int  deviceCountLayout = 0;     // Devices extracted from layout
    int  deviceCountSchematic = 0;  // Devices in schematic netlist
    int  netCountLayout = 0;        // Nets extracted from layout
    int  netCountSchematic = 0;     // Nets in schematic netlist
    int  mismatchCount = 0;         // Total mismatches
    QList<LVSMismatch> mismatches;  // Individual mismatches
    QStringList errors;             // Process-level errors
    QString rawOutput;              // Full stdout for debugging
};

// ============================================================================
// PARSER
// ============================================================================

/**
 * @brief Parses KLayout DRC and LVS report output into structured results.
 *
 * KLayout's batch-mode output is parsed via regex to extract:
 *   - DRC: violation counts, rule names, layers, bounding boxes
 *   - LVS: match/mismatch status, device/net counts, mismatch details
 */
class KLayoutReportParser {
public:
    /**
     * Parse DRC report output from KLayout batch run.
     * Handles both stdout text and marker database (RDB) summary.
     */
    static DRCResult parseDRC(const QString& output);

    /**
     * Parse LVS report output from KLayout batch run.
     * Extracts match status, device/net counts, and mismatches.
     */
    static LVSResult parseLVS(const QString& output);

    /**
     * Parse DRC errors from stderr output and merge into result.
     */
    static void parseDRCErrors(const QString& stderrOutput, DRCResult& result);

    /**
     * Parse LVS errors from stderr output and merge into result.
     */
    static void parseLVSErrors(const QString& stderrOutput, LVSResult& result);
};

} // namespace acutesim::compute::orchestration
