#include "tool_registry.h"
#include <QJsonDocument>

namespace AIAPI {

// ============================================================================
// ToolDefinition
// ============================================================================

QJsonObject ToolDefinition::toJsonSchema() const {
    QJsonObject schema;
    schema["type"] = QString("object");

    QJsonObject properties;
    QJsonArray requiredParams;

    for (const auto& param : parameters) {
        QJsonObject prop;
        prop["type"] = param.type;
        prop["description"] = param.description;
        if (!param.defaultValue.isUndefined()) {
            prop["default"] = param.defaultValue;
        }
        if (!param.enumValues.isEmpty()) {
            prop["enum"] = param.enumValues;
        }
        properties[param.name] = prop;

        if (param.required) {
            requiredParams.append(param.name);
        }
    }

    schema["properties"] = properties;
    if (!requiredParams.isEmpty()) {
        schema["required"] = requiredParams;
    }

    return schema;
}

QJsonObject ToolDefinition::toManifest() const {
    QJsonObject entry;
    entry["name"] = name;
    entry["description"] = description;
    entry["parameters"] = toJsonSchema();
    return entry;
}

// ============================================================================
// ToolRegistry Singleton
// ============================================================================

ToolRegistry::ToolRegistry() {
    registerBuiltinTools();
}

ToolRegistry& ToolRegistry::instance() {
    static ToolRegistry _instance;
    return _instance;
}

void ToolRegistry::registerTool(const ToolDefinition& definition, ToolHandler handler) {
    QMutexLocker locker(&mutex_);
    definitions_[definition.name] = definition;
    handlers_[definition.name] = handler;
}

ToolResult ToolRegistry::invoke(const QString& toolName, const QJsonObject& params) {
    QMutexLocker locker(&mutex_);

    if (!handlers_.contains(toolName)) {
        return ToolResult::error(QString("Unknown tool: %1").arg(toolName));
    }

    // Validate parameters
    QString validationError;
    if (!validateParams(toolName, params, validationError)) {
        return ToolResult::error(validationError);
    }

    auto handler = handlers_[toolName];
    locker.unlock(); // Release lock before calling handler

    return handler(params);
}

QJsonArray ToolRegistry::getToolManifest() const {
    QMutexLocker locker(&mutex_);
    QJsonArray manifest;

    for (auto it = definitions_.begin(); it != definitions_.end(); ++it) {
        manifest.append(it.value().toManifest());
    }

    return manifest;
}

bool ToolRegistry::validateParams(const QString& toolName, const QJsonObject& params, QString& error) const {
    if (!definitions_.contains(toolName)) {
        error = QString("Unknown tool: %1").arg(toolName);
        return false;
    }

    const auto& def = definitions_[toolName];

    // Check required parameters
    for (const auto& param : def.parameters) {
        if (param.required && !params.contains(param.name)) {
            error = QString("Missing required parameter '%1' for tool '%2'")
                        .arg(param.name).arg(toolName);
            return false;
        }
    }

    return true;
}

bool ToolRegistry::hasTool(const QString& name) const {
    QMutexLocker locker(&mutex_);
    return definitions_.contains(name);
}

QStringList ToolRegistry::toolNames() const {
    QMutexLocker locker(&mutex_);
    return definitions_.keys();
}

ToolDefinition ToolRegistry::getToolDefinition(const QString& name) const {
    QMutexLocker locker(&mutex_);
    return definitions_.value(name);
}

// ============================================================================
// Builtin Tool Definitions
// ============================================================================

void ToolRegistry::registerBuiltinTools() {
    // 1. simulate_circuit
    {
        ToolDefinition def;
        def.name = "simulate_circuit";
        def.description = "Run a circuit simulation with specified parameters. Returns simulation trace as JSON.";
        def.parameters = {
            {"analysis_type", "string", "Type of analysis to run", true, {},
             QJsonArray({"DC", "AC", "Transient", "MonteCarlo"})},
            {"duration", "number", "Simulation duration in seconds (for transient)", false, QJsonValue(1e-3), {}},
            {"solver_overrides", "object", "Optional solver parameter overrides", false, {}, {}}
        };
        // Handler registered by tool_handlers.cpp
        definitions_[def.name] = def;
    }

    // 2. retrieve_similar_designs
    {
        ToolDefinition def;
        def.name = "retrieve_similar_designs";
        def.description = "Retrieve similar circuit designs from the knowledge base using semantic search.";
        def.parameters = {
            {"query", "string", "Natural language query describing desired design", true, {}, {}},
            {"topK", "integer", "Number of results to return", false, QJsonValue(5), {}},
            {"filters", "object", "Optional filters (topology, device_type, etc.)", false, {}, {}}
        };
        definitions_[def.name] = def;
    }

    // 3. get_circuit_context
    {
        ToolDefinition def;
        def.name = "get_circuit_context";
        def.description = "Get structured context about the current circuit state.";
        def.parameters = {
            {"detail_level", "string", "Level of detail", false, QJsonValue("medium"),
             QJsonArray({"minimal", "medium", "full"})},
            {"max_tokens", "integer", "Maximum tokens for context", false, QJsonValue(4000), {}}
        };
        definitions_[def.name] = def;
    }

    // 4. check_design_rules
    {
        ToolDefinition def;
        def.name = "check_design_rules";
        def.description = "Run design rule checks and return violations with remediation suggestions.";
        def.parameters = {
            {"rule_categories", "array", "Categories to check (ERC, DRC, LVS)", false, {}, {}}
        };
        definitions_[def.name] = def;
    }

    // 5. get_debug_suggestions
    {
        ToolDefinition def;
        def.name = "get_debug_suggestions";
        def.description = "Get prioritized debug and probe recommendations for the current circuit.";
        def.parameters = {
            {"focus_area", "string", "Optional focus area for suggestions", false, {},
             QJsonArray({"convergence", "bias", "signal_path", "noise", "all"})}
        };
        definitions_[def.name] = def;
    }

    // 6. submit_feedback
    {
        ToolDefinition def;
        def.name = "submit_feedback";
        def.description = "Submit user feedback on a query result for learning.";
        def.parameters = {
            {"query_id", "string", "ID of the query to provide feedback on", true, {}, {}},
            {"rating", "integer", "Rating: 1 (helpful), -1 (unhelpful), 0 (neutral)", true, {}, {}},
            {"correction", "string", "Optional textual correction", false, {}, {}}
        };
        definitions_[def.name] = def;
    }

    // 7. get_counterfactuals
    {
        ToolDefinition def;
        def.name = "get_counterfactuals";
        def.description = "Get counterfactual traces: same topology, different configuration (What-If analysis).";
        def.parameters = {
            {"topology_hash", "string", "Hash of the circuit topology", true, {}, {}},
            {"target_config", "object", "Target configuration to compare against", false, {}, {}}
        };
        definitions_[def.name] = def;
    }

    // 8. compress_context
    {
        ToolDefinition def;
        def.name = "compress_context";
        def.description = "Compress context JSON to fit within a token budget using priority-based allocation.";
        def.parameters = {
            {"context_json", "object", "Full context JSON to compress", true, {}, {}},
            {"max_tokens", "integer", "Maximum token budget", false, QJsonValue(4000), {}},
            {"priority_mode", "string", "Allocation strategy", false, QJsonValue("strict"),
             QJsonArray({"strict", "balanced", "debug"})}
        };
        definitions_[def.name] = def;
    }
}

} // namespace AIAPI
