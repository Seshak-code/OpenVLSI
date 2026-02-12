#include "yosys_log_parser.h"
#include <QRegularExpression>

namespace acutesim::compute::orchestration {

// ============================================================================
// MODULE BLOCK PARSER
// ============================================================================

ModuleStats YosysLogParser::parseModuleBlock(const QStringList& lines, int startLine, int& endLine) {
    ModuleStats stats;
    
    // Extract module name from "=== <name> ==="
    static QRegularExpression moduleRe(R"(^=== (\S+) ===$)");
    auto match = moduleRe.match(lines[startLine].trimmed());
    if (match.hasMatch()) {
        stats.moduleName = match.captured(1);
    }
    
    // Parse stat lines until next "===" or end
    static QRegularExpression statRe(R"(^\s+Number of (\w[\w ]*\w)\s*:\s+(\d+))");
    static QRegularExpression cellRe(R"(^\s{4,}(\S+)\s+(\d+))");
    static QRegularExpression areaRe(R"(Chip area for module '\\?(\S+)':\s+([\d.]+))");
    
    bool inCellList = false;
    
    for (int i = startLine + 1; i < lines.size(); ++i) {
        const QString& line = lines[i];
        QString trimmed = line.trimmed();
        
        // Next module block starts
        if (trimmed.startsWith("===") && trimmed.endsWith("===") && i > startLine + 1) {
            endLine = i - 1;
            return stats;
        }

        // "Number of X: N" lines    
        auto statMatch = statRe.match(line);
        if (statMatch.hasMatch()) {
            QString key = statMatch.captured(1).trimmed();
            int value = statMatch.captured(2).toInt();
            
            if (key == "wires")          stats.wireCount = value;
            else if (key == "wire bits") stats.wireBits = value;
            else if (key == "public wires") stats.publicWires = value;
            else if (key == "cells")     { stats.cellCount = value; inCellList = true; }
            else if (key == "memories")  stats.memoryBits = value;
            else if (key == "processes") stats.processes = value;
            
            if (key != "cells") inCellList = false;
            continue;
        }
        
        // Cell breakdown lines (indented, e.g. "      $_NOT_    4")
        if (inCellList) {
            auto cellMatch = cellRe.match(line);
            if (cellMatch.hasMatch()) {
                stats.cellBreakdown[cellMatch.captured(1)] = cellMatch.captured(2).toInt();
                continue;
            } else if (!trimmed.isEmpty()) {
                inCellList = false;
            }
        }
        
        // "Chip area for module '\top': 1234.560000"
        auto areaMatch = areaRe.match(line);
        if (areaMatch.hasMatch()) {
            stats.chipArea = areaMatch.captured(2).toDouble();
        }
    }
    
    endLine = lines.size() - 1;
    return stats;
}

// ============================================================================
// MAIN PARSER
// ============================================================================

SynthesisResult YosysLogParser::parse(const QString& output) {
    SynthesisResult result;
    result.rawOutput = output;
    
    QStringList lines = output.split('\n');
    
    // Extract module stat blocks
    for (int i = 0; i < lines.size(); ++i) {
        QString trimmed = lines[i].trimmed();
        
        // Module stat block header: "=== <name> ==="
        static QRegularExpression moduleHeaderRe(R"(^=== (\S+) ===$)");
        if (moduleHeaderRe.match(trimmed).hasMatch()) {
            int endLine = i;
            ModuleStats modStats = parseModuleBlock(lines, i, endLine);
            result.modules[modStats.moduleName] = modStats;
            
            // The last module reported is typically the top
            result.cellCount = modStats.cellCount;
            result.wireCount = modStats.wireCount;
            result.estimatedArea = modStats.chipArea;
            
            // Merge cell breakdown
            for (auto it = modStats.cellBreakdown.constBegin(); 
                 it != modStats.cellBreakdown.constEnd(); ++it) {
                result.cellBreakdown[it.key()] += it.value();
            }
            
            i = endLine;
            continue;
        }
        
        // Warning lines
        if (trimmed.startsWith("Warning:") || trimmed.startsWith("warning:")) {
            result.warnings.append(trimmed);
        }
    }
    
    // Parse errors separately
    parseErrors(output, result);
    
    // Determine success: no ERROR lines found
    result.success = result.errors.isEmpty();
    
    return result;
}

// ============================================================================
// ERROR PARSER
// ============================================================================

void YosysLogParser::parseErrors(const QString& stderr, SynthesisResult& result) {
    QStringList lines = stderr.split('\n');
    
    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.startsWith("ERROR:") || trimmed.startsWith("error:")) {
            result.errors.append(trimmed);
        }
    }
}

// ============================================================================
// SUCCESS CHECK
// ============================================================================

bool YosysLogParser::isSuccessful(const QString& output) {
    return !output.contains("ERROR:");
}

} // namespace acutesim::compute::orchestration
