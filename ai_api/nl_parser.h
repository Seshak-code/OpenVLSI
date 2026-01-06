#pragma once
#include <QString>
#include <QJsonObject>
#include <QRegularExpression>

/**
 * nl_parser.h - Natural Language Intent Parser
 * 
 * A rule-based parser for extracting structured intents from user commands.
 * Phase 1: Simple regex patterns. Phase 2: LLM integration.
 */

namespace AIAPI {

struct ParsedIntent {
    QString action;           // e.g., "placeComponent", "deleteComponent", "instantiateTemplate"
    QJsonObject params;       // Action-specific parameters
    double confidence;        // 0.0 to 1.0
    QString rawMatch;         // The matched pattern for debugging
};

class NLParser {
public:
    /**
     * parseIntent - Extract structured intent from natural language text.
     * 
     * Supported patterns:
     *   - "add/place [value] resistor [at x,y]" → placeComponent
     *   - "delete [component id]" → deleteComponent
     *   - "design/create [topology]" → instantiateTemplate
     */
    static ParsedIntent parseIntent(const QString& text) {
        ParsedIntent result;
        result.confidence = 0.0;
        
        QString lower = text.toLower().trimmed();
        
        // Pattern 1: Place resistor with optional value and coordinates
        // "add a 10k resistor at 100,200" or "place resistor"
        QRegularExpression resistorPattern(
            R"((?:add|place)\s+(?:a\s+)?(\d+(?:\.\d+)?[kmKM]?)?\s*(?:ohm\s+)?resistor(?:\s+at\s+(\d+)\s*,\s*(\d+))?)",
            QRegularExpression::CaseInsensitiveOption);
        auto match = resistorPattern.match(lower);
        if (match.hasMatch()) {
            result.action = "placeComponent";
            result.params["type"] = "Resistor";
            result.params["value"] = match.captured(1).isEmpty() ? "1k" : match.captured(1);
            if (!match.captured(2).isEmpty()) {
                result.params["x"] = match.captured(2).toInt();
                result.params["y"] = match.captured(3).toInt();
            }
            result.confidence = 0.9;
            result.rawMatch = match.captured(0);
            return result;
        }
        
        // Pattern 2: Place capacitor
        QRegularExpression capacitorPattern(
            R"((?:add|place)\s+(?:a\s+)?(\d+(?:\.\d+)?[pnumPNUM]?[fF]?)?\s*capacitor(?:\s+at\s+(\d+)\s*,\s*(\d+))?)",
            QRegularExpression::CaseInsensitiveOption);
        match = capacitorPattern.match(lower);
        if (match.hasMatch()) {
            result.action = "placeComponent";
            result.params["type"] = "Capacitor";
            result.params["value"] = match.captured(1).isEmpty() ? "1n" : match.captured(1);
            if (!match.captured(2).isEmpty()) {
                result.params["x"] = match.captured(2).toInt();
                result.params["y"] = match.captured(3).toInt();
            }
            result.confidence = 0.9;
            result.rawMatch = match.captured(0);
            return result;
        }
        
        // Pattern 3: Delete component
        QRegularExpression deletePattern(
            R"((?:delete|remove)\s+([A-Za-z]\d+))",
            QRegularExpression::CaseInsensitiveOption);
        match = deletePattern.match(lower);
        if (match.hasMatch()) {
            result.action = "deleteComponent";
            result.params["id"] = match.captured(1).toUpper();
            result.confidence = 0.95;
            result.rawMatch = match.captured(0);
            return result;
        }
        
        // Pattern 4: Circuit templates (topologies)
        QRegularExpression templatePattern(
            R"((?:design|create|add|make)\s+(?:a\s+)?(?:an?\s+)?(darlington|current\s*mirror|diff(?:erential)?\s*pair|inverter|buffer|source\s*follower))",
            QRegularExpression::CaseInsensitiveOption);
        match = templatePattern.match(lower);
        if (match.hasMatch()) {
            result.action = "instantiateTemplate";
            QString templateName = match.captured(1).toLower().replace(" ", "_");
            // Normalize common variations
            if (templateName.contains("diff")) templateName = "differential_pair";
            if (templateName.contains("mirror")) templateName = "current_mirror";
            result.params["template"] = templateName;
            result.confidence = 0.85;
            result.rawMatch = match.captured(0);
            return result;
        }
        
        // Pattern 5: Simulate command
        QRegularExpression simPattern(
            R"((?:run|start|do)\s+(?:a\s+)?(?:dc\s+)?(?:simulation|sim|analysis))",
            QRegularExpression::CaseInsensitiveOption);
        match = simPattern.match(lower);
        if (match.hasMatch()) {
            result.action = "runSimulation";
            result.params["type"] = "DC";
            result.confidence = 0.9;
            result.rawMatch = match.captured(0);
            return result;
        }
        
        // No match found
        result.action = "";
        result.confidence = 0.0;
        return result;
    }
};

} // namespace AIAPI
