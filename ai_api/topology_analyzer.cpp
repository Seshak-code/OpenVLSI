#include "topology_analyzer.h"
#include "context_engine.h"
#include <QJsonArray>
#include <QMap>
#include <QSet>
#include <algorithm>

namespace AIAPI {

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

static QString blockTypeName(FunctionalBlockType type) {
    switch (type) {
        case FunctionalBlockType::COMMON_EMITTER: return "CE";
        case FunctionalBlockType::COMMON_COLLECTOR: return "EF";
        case FunctionalBlockType::COMMON_BASE: return "CB";
        case FunctionalBlockType::COMMON_SOURCE: return "CS";
        case FunctionalBlockType::COMMON_DRAIN: return "SF";
        case FunctionalBlockType::COMMON_GATE: return "CG";
        case FunctionalBlockType::DIFFERENTIAL_PAIR: return "DiffPair";
        case FunctionalBlockType::DARLINGTON_PAIR: return "Darlington";
        case FunctionalBlockType::CASCODE: return "Cascode";
        case FunctionalBlockType::RESISTOR_DIVIDER: return "RDiv";
        case FunctionalBlockType::CURRENT_MIRROR: return "IMirror";
        case FunctionalBlockType::BETA_MULTIPLIER: return "BetaMult";
        case FunctionalBlockType::BANDGAP_REFERENCE: return "Vbg";
        case FunctionalBlockType::NEGATIVE_FEEDBACK: return "NFB";
        case FunctionalBlockType::EMITTER_DEGENERATION: return "RE_Degen";
        case FunctionalBlockType::SOURCE_DEGENERATION: return "RS_Degen";
        case FunctionalBlockType::POWER_STAGE: return "Power";
        case FunctionalBlockType::PUSH_PULL: return "PushPull";
        case FunctionalBlockType::RC_FILTER: return "RCFilt";
        case FunctionalBlockType::LC_TANK: return "LCTank";
        case FunctionalBlockType::DECOUPLING_NETWORK: return "Decap";
        default: return "Unknown";
    }
}

static QString blockTypeDescription(FunctionalBlockType type) {
    switch (type) {
        case FunctionalBlockType::COMMON_EMITTER: return "Common-Emitter Amplifier";
        case FunctionalBlockType::COMMON_COLLECTOR: return "Emitter Follower (Unity Gain Buffer)";
        case FunctionalBlockType::DIFFERENTIAL_PAIR: return "Differential Input Stage";
        case FunctionalBlockType::DARLINGTON_PAIR: return "High-Beta Darlington Cascade";
        case FunctionalBlockType::CURRENT_MIRROR: return "Current Mirror (Biasing)";
        case FunctionalBlockType::RESISTOR_DIVIDER: return "Resistive Voltage Divider";
        case FunctionalBlockType::EMITTER_DEGENERATION: return "Emitter Degeneration (Feedback)";
        default: return "Analog Block";
    }
}

// ============================================================================
// PATTERN MATCHERS
// ============================================================================

bool TopologyAnalyzer::matchDifferentialPair(const QJsonArray& components, FunctionalBlock& out) {
    // Look for 2 BJTs with shared emitter node
    QList<QJsonObject> bjts;
    
    for (const auto& val : components) {
        QJsonObject c = val.toObject();
        if (c["type"].toString() == "Bjt") {
            bjts.append(c);
        }
    }
    
    if (bjts.size() < 2) return false;
    
    // Simple heuristic: if two BJTs are placed close together and have similar names
    // (e.g., Q1, Q2), assume diff pair
    bool found = false;
    for (int i = 0; i < bjts.size() - 1; ++i) {
        QJsonObject q1 = bjts[i];
        QJsonObject q2 = bjts[i + 1];
        
        QString n1 = q1["name"].toString();
        QString n2 = q2["name"].toString();
        
        // Check if they're sequential (Q1, Q2)
        if (n1.left(1) == n2.left(1)) {
            out.type = FunctionalBlockType::DIFFERENTIAL_PAIR;
            out.name = QString("%1-%2 Diff Pair").arg(n1).arg(n2);
            out.componentIds.push_back(n1);
            out.componentIds.push_back(n2);
            out.estimatedGain = 50.0; // Typical diff pair gain
            out.description = blockTypeDescription(out.type);
            found = true;
            break;
        }
    }
    
    return found;
}

bool TopologyAnalyzer::matchCurrentMirror(const QJsonArray& components, FunctionalBlock& out) {
    // Current mirror: 2 transistors, one diode-connected
    QList<QJsonObject> transistors;
    
    for (const auto& val : components) {
        QJsonObject c = val.toObject();
        QString type = c["type"].toString();
        if (type == "Bjt" || type == "Mosfet") {
            transistors.append(c);
        }
    }
    
    if (transistors.size() < 2) return false;
    
    // Heuristic: Look for transistors with matching model types
    QMap<QString, QList<QJsonObject>> byModel;
    for (const auto& t : transistors) {
        QString model = t["properties"].toObject()["Model"].toString();
        byModel[model].append(t);
    }
    
    // If we have 2+ same-type transistors, likely a mirror
    for (auto it = byModel.begin(); it != byModel.end(); ++it) {
        if (it.value().size() >= 2) {
            out.type = FunctionalBlockType::CURRENT_MIRROR;
            QString t1 = it.value()[0]["name"].toString();
            QString t2 = it.value()[1]["name"].toString();
            out.name = QString("%1-%2 Mirror").arg(t1).arg(t2);
            out.componentIds.push_back(t1);
            out.componentIds.push_back(t2);
            out.estimatedGain = 1.0; // Unity current gain (ideal)
            out.description = blockTypeDescription(out.type);
            return true;
        }
    }
    
    return false;
}

bool TopologyAnalyzer::matchDarlington(const QJsonArray& components, FunctionalBlock& out) {
    // Darlington: 2 BJTs in cascade (Q1.E → Q2.B)
    QList<QJsonObject> bjts;
    
    for (const auto& val : components) {
        QJsonObject c = val.toObject();
        if (c["type"].toString() == "Bjt") {
            bjts.append(c);
        }
    }
    
    if (bjts.size() < 2) return false;
    
    // Simple heuristic: sequential BJTs with NPN model
    for (int i = 0; i < bjts.size() - 1; ++i) {
        QJsonObject q1 = bjts[i];
        QJsonObject q2 = bjts[i + 1];
        
        QString m1 = q1["properties"].toObject()["Model"].toString();
        QString m2 = q2["properties"].toObject()["Model"].toString();
        
        if (m1.contains("npn") && m2.contains("npn")) {
            out.type = FunctionalBlockType::DARLINGTON_PAIR;
            QString n1 = q1["name"].toString();
            QString n2 = q2["name"].toString();
            out.name = QString("%1-%2 Darlington").arg(n1).arg(n2);
            out.componentIds.push_back(n1);
            out.componentIds.push_back(n2);
            out.estimatedGain = 10000.0; // Beta^2
            out.description = blockTypeDescription(out.type);
            return true;
        }
    }
    
    return false;
}

bool TopologyAnalyzer::matchCommonEmitter(const QJsonArray& components, FunctionalBlock& out) {
    // CE: Single BJT with collector resistor
    QList<QJsonObject> bjts;
    QList<QJsonObject> resistors;
    
    for (const auto& val : components) {
        QJsonObject c = val.toObject();
        QString type = c["type"].toString();
        if (type == "Bjt") bjts.append(c);
        if (type == "Resistor") resistors.append(c);
    }
    
    if (bjts.isEmpty() || resistors.isEmpty()) return false;
    
    // For each BJT, check if there's a collector resistor
    for (const auto& bjt : bjts) {
        QString bjtName = bjt["name"].toString();
        
        // Assume Rc exists if we have a resistor
        out.type = FunctionalBlockType::COMMON_EMITTER;
        out.name = QString("%1 CE Stage").arg(bjtName);
        out.componentIds.push_back(bjtName);
        
        // Add first resistor as assumed Rc
        if (!resistors.isEmpty()) {
            out.componentIds.push_back(resistors[0]["name"].toString());
        }
        
        out.estimatedGain = -40.0; // Typical CE gain (inverted)
        out.description = blockTypeDescription(out.type);
        return true;
    }
    
    return false;
}

bool TopologyAnalyzer::matchResistorDivider(const QJsonArray& components, FunctionalBlock& out) {
    // Divider: 2 resistors in series
    QList<QJsonObject> resistors;
    
    for (const auto& val : components) {
        QJsonObject c = val.toObject();
        if (c["type"].toString() == "Resistor") {
            resistors.append(c);
        }
    }
    
    if (resistors.size() >= 2) {
        out.type = FunctionalBlockType::RESISTOR_DIVIDER;
        QString r1 = resistors[0]["name"].toString();
        QString r2 = resistors[1]["name"].toString();
        out.name = QString("%1-%2 Divider").arg(r1).arg(r2);
        out.componentIds.push_back(r1);
        out.componentIds.push_back(r2);
        out.estimatedGain = 0.5; // Approximate
        out.description = blockTypeDescription(out.type);
        return true;
    }
    
    return false;
}

bool TopologyAnalyzer::matchEmitterDegeneration(const QJsonArray& components, FunctionalBlock& out) {
    // RE: BJT with emitter resistor
    bool hasBjt = false;
    QString bjtName;
    int resistorCount = 0;
    
    for (const auto& val : components) {
        QJsonObject c = val.toObject();
        QString type = c["type"].toString();
        if (type == "Bjt") {
            hasBjt = true;
            bjtName = c["name"].toString();
        }
        if (type == "Resistor") resistorCount++;
    }
    
    // If we have BJT and multiple resistors, assume one is RE
    if (hasBjt && resistorCount >= 2) {
        out.type = FunctionalBlockType::EMITTER_DEGENERATION;
        out.name = QString("%1 + RE").arg(bjtName);
        out.componentIds.push_back(bjtName);
        out.estimatedGain = 0.0; // Local feedback reduces gain
        out.description = blockTypeDescription(out.type);
        return true;
    }
    
    return false;
}

// ============================================================================
// GAIN ESTIMATION
// ============================================================================

double TopologyAnalyzer::estimateGain(FunctionalBlockType type, const QJsonObject& params) {
    switch (type) {
        case FunctionalBlockType::COMMON_EMITTER: return -40.0;
        case FunctionalBlockType::COMMON_COLLECTOR: return 0.99;
        case FunctionalBlockType::COMMON_SOURCE: return -30.0;
        case FunctionalBlockType::COMMON_DRAIN: return 0.95;
        case FunctionalBlockType::DIFFERENTIAL_PAIR: return 50.0;
        case FunctionalBlockType::DARLINGTON_PAIR: return 10000.0;
        case FunctionalBlockType::CASCODE: return 200.0;
        case FunctionalBlockType::CURRENT_MIRROR: return 1.0;
        case FunctionalBlockType::RESISTOR_DIVIDER: return 0.5;
        default: return 1.0;
    }
}

// ============================================================================
// SHORTHAND COMPRESSION
// ============================================================================

QString TopologyAnalyzer::blockToShorthand(const FunctionalBlock& block) {
    QString ids = "";
    for (const auto& id : block.componentIds) {
        if (!ids.isEmpty()) ids += ",";
        ids += id;
    }
    
    QString gainStr = "";
    if (block.estimatedGain != 0.0 && block.type != FunctionalBlockType::UNKNOWN) {
        gainStr = QString(",Av≈%1").arg(block.estimatedGain, 0, 'g', 3);
    }
    
    return QString("%1(%2%3)").arg(blockTypeName(block.type)).arg(ids).arg(gainStr);
}

QString TopologyAnalyzer::pathToShorthand(const SignalPath& path) {
    QString result = path.source + "→";
    for (const auto& block : path.blockSequence) {
        result += block + "→";
    }
    result += path.sink;
    return result;
}

// ============================================================================
// MAIN ANALYSIS FUNCTIONS
// ============================================================================

std::vector<FunctionalBlock> TopologyAnalyzer::identifyFunctionalBlocks() {
    std::vector<FunctionalBlock> blocks;
    QJsonObject state = ContextEngine::getContextJson();
    QJsonArray components = state["components"].toArray();
    
    if (components.isEmpty()) return blocks;
    
    // Try each pattern matcher in priority order
    FunctionalBlock block;
    
    // 1. Differential Pair (highest complexity)
    if (matchDifferentialPair(components, block)) {
        blocks.push_back(block);
    }
    
    // 2. Darlington (before simple CE)
    block = FunctionalBlock{};
    if (matchDarlington(components, block)) {
        blocks.push_back(block);
    }
    
    // 3. Current Mirror
    block = FunctionalBlock{};
    if (matchCurrentMirror(components, block)) {
        blocks.push_back(block);
    }
    
    // 4. Common Emitter (fallback for single BJT)
    block = FunctionalBlock{};
    if (matchCommonEmitter(components, block)) {
        // Only add if we haven't already identified this transistor
        bool alreadyIdentified = false;
        for (const auto& existing : blocks) {
            for (const auto& id : existing.componentIds) {
                for (const auto& newId : block.componentIds) {
                    if (id == newId) alreadyIdentified = true;
                }
            }
        }
        if (!alreadyIdentified) {
            blocks.push_back(block);
        }
    }
    
    // 5. Resistor Divider (biasing)
    block = FunctionalBlock{};
    if (matchResistorDivider(components, block)) {
        blocks.push_back(block);
    }
    
    // 6. Emitter Degeneration (feedback)
    block = FunctionalBlock{};
    if (matchEmitterDegeneration(components, block)) {
        blocks.push_back(block);
    }
    
    return blocks;
}

std::vector<SignalPath> TopologyAnalyzer::extractSignalPaths() {
    std::vector<SignalPath> paths;
    std::vector<FunctionalBlock> blocks = identifyFunctionalBlocks();
    
    QJsonObject state = ContextEngine::getContextJson();
    QJsonArray components = state["components"].toArray();
    
    // Identify sources and sinks
    QString inputSource = "Vin";
    QString outputSink = "Vout";
    
    for (const auto& val : components) {
        QJsonObject c = val.toObject();
        QString type = c["type"].toString();
        QString name = c["name"].toString();
        
        if (type == "VoltageSource" || type == "CurrentSource") {
            inputSource = name;
        }
    }
    
    // Build path from blocks
    SignalPath mainPath;
    mainPath.pathId = "P1";
    mainPath.source = inputSource;
    mainPath.sink = outputSink;
    
    for (const auto& block : blocks) {
        // Only include amplification/signal blocks (not bias)
        if (block.type == FunctionalBlockType::COMMON_EMITTER ||
            block.type == FunctionalBlockType::COMMON_COLLECTOR ||
            block.type == FunctionalBlockType::DIFFERENTIAL_PAIR ||
            block.type == FunctionalBlockType::DARLINGTON_PAIR) {
            mainPath.blockSequence.push_back(blockTypeName(block.type));
            mainPath.probePoints.push_back(block.outputNode);
        }
    }
    
    mainPath.compressedSummary = pathToShorthand(mainPath);
    paths.push_back(mainPath);
    
    return paths;
}

QString TopologyAnalyzer::generateCompressedPrompt() {
    std::vector<FunctionalBlock> blocks = identifyFunctionalBlocks();
    std::vector<SignalPath> paths = extractSignalPaths();
    
    if (blocks.empty()) {
        return "Circuit: Unanalyzed (no recognized patterns).";
    }
    
    QString result = "Circuit: ";
    
    // Count stages
    int ampStages = 0;
    for (const auto& b : blocks) {
        if (b.type == FunctionalBlockType::COMMON_EMITTER ||
            b.type == FunctionalBlockType::COMMON_COLLECTOR ||
            b.type == FunctionalBlockType::COMMON_SOURCE ||
            b.type == FunctionalBlockType::DIFFERENTIAL_PAIR ||
            b.type == FunctionalBlockType::DARLINGTON_PAIR) {
            ampStages++;
        }
    }
    
    result += QString("%1-stage amp. ").arg(ampStages);
    
    // Block summaries
    int stageNum = 1;
    for (const auto& b : blocks) {
        if (b.type != FunctionalBlockType::RESISTOR_DIVIDER &&
            b.type != FunctionalBlockType::EMITTER_DEGENERATION) {
            result += QString("Stage%1: %2. ").arg(stageNum++).arg(blockToShorthand(b));
        }
    }
    
    // Path summary
    if (!paths.empty()) {
        result += QString("Path: %1. ").arg(paths[0].compressedSummary);
    }
    
    // Bias summary
    for (const auto& b : blocks) {
        if (b.type == FunctionalBlockType::RESISTOR_DIVIDER) {
            result += QString("Bias: %1.").arg(b.name);
            break;
        }
    }
    
    return result.trimmed();
}

QJsonObject TopologyAnalyzer::toJson() {
    QJsonObject result;
    
    // Blocks
    QJsonArray blockArray;
    for (const auto& b : identifyFunctionalBlocks()) {
        QJsonObject bo;
        bo["type"] = blockTypeName(b.type);
        bo["name"] = b.name;
        bo["gain"] = b.estimatedGain;
        bo["description"] = b.description;
        
        QJsonArray ids;
        for (const auto& id : b.componentIds) {
            ids.append(id);
        }
        bo["components"] = ids;
        
        blockArray.append(bo);
    }
    result["functionalBlocks"] = blockArray;
    
    // Paths
    QJsonArray pathArray;
    for (const auto& p : extractSignalPaths()) {
        QJsonObject po;
        po["id"] = p.pathId;
        po["source"] = p.source;
        po["sink"] = p.sink;
        po["summary"] = p.compressedSummary;
        
        QJsonArray seq;
        for (const auto& s : p.blockSequence) {
            seq.append(s);
        }
        po["blocks"] = seq;
        
        pathArray.append(po);
    }
    result["signalPaths"] = pathArray;
    
    // Compressed prompt
    result["compressedPrompt"] = generateCompressedPrompt();
    
    return result;
}

} // namespace AIAPI
