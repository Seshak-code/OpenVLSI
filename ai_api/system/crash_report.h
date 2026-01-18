#pragma once

#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QStandardPaths>

/**
 * crash_report.h - Debug Logging & Crash Report System
 * 
 * Captures comprehensive circuit state when errors occur:
 * - Component positions and properties
 * - Wire connectivity
 * - Probe tip positions and intersections
 * - Netlist node assignments
 * 
 * Reports are saved to ~/.beaversim/crash_logs/ and can be
 * injected into AI copilot context for debugging assistance.
 */

namespace DebugLog {

// ============================================================================
// CRASH REPORT STRUCTURE
// ============================================================================

struct ProbeDebugInfo {
    QString name;
    double tipPosX, tipPosY;
    double tipNegX, tipNegY;
    int detectedNodePos = -1;
    int detectedNodeNeg = -1;
    bool posConnected = false;
    bool negConnected = false;
    QString intersectedWirePos;
    QString intersectedWireNeg;
    
    QJsonObject toJson() const {
        return {
            {"name", name},
            {"tipPositive", QJsonObject{{"x", tipPosX}, {"y", tipPosY}, {"node", detectedNodePos}, {"connected", posConnected}, {"wire", intersectedWirePos}}},
            {"tipNegative", QJsonObject{{"x", tipNegX}, {"y", tipNegY}, {"node", detectedNodeNeg}, {"connected", negConnected}, {"wire", intersectedWireNeg}}}
        };
    }
};

struct ComponentDebugInfo {
    QString name;
    QString type;
    double x, y;
    double rotation;
    QStringList terminalNodes;  // e.g., ["node3", "node0"]
    QJsonObject properties;
    
    QJsonObject toJson() const {
        QJsonArray terms;
        for (const auto& t : terminalNodes) terms.append(t);
        return {
            {"name", name},
            {"type", type},
            {"position", QJsonObject{{"x", x}, {"y", y}, {"rotation", rotation}}},
            {"terminals", terms},
            {"properties", properties}
        };
    }
};

struct WireDebugInfo {
    double x1, y1, x2, y2;
    QString netName;
    int assignedNodeId = -1;
    
    QJsonObject toJson() const {
        return {
            {"p1", QJsonObject{{"x", x1}, {"y", y1}}},
            {"p2", QJsonObject{{"x", x2}, {"y", y2}}},
            {"netName", netName},
            {"nodeId", assignedNodeId}
        };
    }
};

struct NetlistDebugInfo {
    int numGlobalNodes = 0;
    int numResistors = 0;
    int numCapacitors = 0;
    int numVoltageSources = 0;
    int numCurrentSources = 0;
    int numDiodes = 0;
    int numBJTs = 0;
    int numMosfets = 0;
    int numProbes = 0;
    QStringList nodeAssignments;  // e.g., ["node0=GND", "node1=(100,200)", ...]
    
    QJsonObject toJson() const {
        QJsonArray nodes;
        for (const auto& n : nodeAssignments) nodes.append(n);
        return {
            {"numGlobalNodes", numGlobalNodes},
            {"components", QJsonObject{
                {"resistors", numResistors},
                {"capacitors", numCapacitors},
                {"voltageSources", numVoltageSources},
                {"currentSources", numCurrentSources},
                {"diodes", numDiodes},
                {"bjts", numBJTs},
                {"mosfets", numMosfets},
                {"probes", numProbes}
            }},
            {"nodeAssignments", nodes}
        };
    }
};

struct CrashReport {
    QString timestamp;
    QString crashType;       // "MATRIX_MISMATCH", "FLOATING_NODE", "CONVERGENCE_FAIL", etc.
    QString errorMessage;
    QString stackTrace;
    
    QList<ComponentDebugInfo> components;
    QList<WireDebugInfo> wires;
    QList<ProbeDebugInfo> probes;
    NetlistDebugInfo netlist;
    
    QJsonObject toJson() const {
        QJsonArray comps, wrs, prbs;
        for (const auto& c : components) comps.append(c.toJson());
        for (const auto& w : wires) wrs.append(w.toJson());
        for (const auto& p : probes) prbs.append(p.toJson());
        
        return {
            {"timestamp", timestamp},
            {"crashType", crashType},
            {"errorMessage", errorMessage},
            {"components", comps},
            {"wires", wrs},
            {"probes", prbs},
            {"netlist", netlist.toJson()}
        };
    }
    
    QString toMarkdown() const {
        QString md;
        md += "# Crash Report: " + crashType + "\n\n";
        md += "**Time**: " + timestamp + "\n\n";
        md += "**Error**: " + errorMessage + "\n\n";
        
        md += "## Netlist Summary\n";
        md += QString("- Nodes: %1\n").arg(netlist.numGlobalNodes);
        md += QString("- Resistors: %1, Capacitors: %2\n").arg(netlist.numResistors).arg(netlist.numCapacitors);
        md += QString("- BJTs: %1, MOSFETs: %2\n").arg(netlist.numBJTs).arg(netlist.numMosfets);
        md += QString("- Probes: %1\n\n").arg(netlist.numProbes);
        
        md += "## Probe State\n";
        for (const auto& p : probes) {
            md += QString("- **%1**: +tip(%2,%3)→node%4 %5, -tip(%6,%7)→node%8 %9\n")
                .arg(p.name)
                .arg(p.tipPosX).arg(p.tipPosY).arg(p.detectedNodePos).arg(p.posConnected ? "✓" : "✗")
                .arg(p.tipNegX).arg(p.tipNegY).arg(p.detectedNodeNeg).arg(p.negConnected ? "✓" : "✗");
        }
        
        md += "\n## Node Assignments\n";
        for (const auto& n : netlist.nodeAssignments) {
            md += "- " + n + "\n";
        }
        
        return md;
    }
};

// ============================================================================
// LOGGING FUNCTIONS
// ============================================================================

inline QString getCrashLogDir() {
    QString dataLocation = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString logDir = dataLocation + "/crash_logs";
    QDir().mkpath(logDir);
    return logDir;
}

inline QString logCrashReport(const CrashReport& report) {
    QString logDir = getCrashLogDir();
    QString filename = QString("crash_%1.json")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
    QString filepath = logDir + "/" + filename;
    
    QFile file(filepath);
    if (file.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(report.toJson());
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
    }
    
    // Also write markdown version for human readability
    QString mdPath = logDir + "/" + filename.replace(".json", ".md");
    QFile mdFile(mdPath);
    if (mdFile.open(QIODevice::WriteOnly)) {
        QTextStream out(&mdFile);
        out << report.toMarkdown();
        mdFile.close();
    }
    
    return filepath;
}

inline QString loadLatestCrashContext() {
    QString logDir = getCrashLogDir();
    QDir dir(logDir);
    QStringList filters = {"crash_*.md"};
    dir.setNameFilters(filters);
    dir.setSorting(QDir::Time);
    
    QStringList entries = dir.entryList();
    if (entries.isEmpty()) return QString();
    
    QString latestPath = logDir + "/" + entries.first();
    QFile file(latestPath);
    if (!file.open(QIODevice::ReadOnly)) return QString();
    
    return QString::fromUtf8(file.readAll());
}

} // namespace DebugLog
