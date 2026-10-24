#include "tool_handlers.h"
#include "../context/context_engine.h"
#include "../context/context_budget.h"
#include "../system/feedback_loop.h"
#include "../simulation/local_vector_store.h"
#include "../analysis/topology_analyzer.h"
#include <QJsonDocument>

namespace AIAPI {

// Shared vector store instance for tool handlers
static LocalVectorStore& getVectorStore() {
    static LocalVectorStore store;
    return store;
}

namespace ToolHandlers {

void registerAll() {
    auto& registry = ToolRegistry::instance();

    registry.registerTool(registry.getToolDefinition("simulate_circuit"), handleSimulateCircuit);
    registry.registerTool(registry.getToolDefinition("retrieve_similar_designs"), handleRetrieveSimilarDesigns);
    registry.registerTool(registry.getToolDefinition("get_circuit_context"), handleGetCircuitContext);
    registry.registerTool(registry.getToolDefinition("check_design_rules"), handleCheckDesignRules);
    registry.registerTool(registry.getToolDefinition("get_debug_suggestions"), handleGetDebugSuggestions);
    registry.registerTool(registry.getToolDefinition("submit_feedback"), handleSubmitFeedback);
    registry.registerTool(registry.getToolDefinition("get_counterfactuals"), handleGetCounterfactuals);
    registry.registerTool(registry.getToolDefinition("compress_context"), handleCompressContext);
}

ToolResult handleSimulateCircuit(const QJsonObject& params) {
    // Delegate to simulation subsystem
    // In a full implementation, this would invoke SimulationWorker
    QString analysisType = params["analysis_type"].toString();
    double duration = params["duration"].toDouble(1e-3);

    QJsonObject result;
    result["analysis_type"] = analysisType;
    result["duration"] = duration;
    result["status"] = QString("queued");
    result["message"] = QString("Simulation '%1' queued. Duration: %2s")
                            .arg(analysisType).arg(duration, 0, 'e', 2);

    return ToolResult::ok(result);
}

ToolResult handleRetrieveSimilarDesigns(const QJsonObject& params) {
    QString query = params["query"].toString();
    int topK = params["topK"].toInt(5);

    auto& store = getVectorStore();
    auto traces = store.querySimilar(query, topK);

    QJsonArray results;
    for (const auto& trace : traces) {
        QJsonObject entry;
        entry["traceId"] = trace.traceId;
        entry["topologyHash"] = trace.topologyHash;
        entry["isFailure"] = trace.isFailure();

        QJsonObject mix;
        for (auto it = trace.deviceMix.begin(); it != trace.deviceMix.end(); ++it) {
            mix[it.key()] = it.value();
        }
        entry["deviceMix"] = mix;
        results.append(entry);
    }

    QJsonObject data;
    data["query"] = query;
    data["count"] = results.size();
    data["results"] = results;
    return ToolResult::ok(data);
}

ToolResult handleGetCircuitContext(const QJsonObject& params) {
    QString detailLevel = params["detail_level"].toString("medium");
    int maxTokens = params["max_tokens"].toInt(4000);

    QJsonObject context;

    if (detailLevel == "minimal") {
        context["topology"] = ContextEngine::serializeTopologyChunks();
    } else if (detailLevel == "full") {
        context["topology"] = ContextEngine::serializeTopologyChunks();
        context["probes"] = ContextEngine::serializeProbeContext();
        context["solver"] = ContextEngine::serializeSolverDecisions();
        context["signalPaths"] = ContextEngine::analyzeSignalPaths();
        context["debugSuggestions"] = ContextEngine::getDebugSuggestions();
    } else {
        // medium
        context["topology"] = ContextEngine::serializeTopologyChunks();
        context["probes"] = ContextEngine::serializeProbeContext();
        context["solver"] = ContextEngine::serializeSolverDecisions();
    }

    // Compress to budget
    auto allocation = ContextBudgetManager::compressForBudget(context, maxTokens);

    QJsonObject data;
    data["detail_level"] = detailLevel;
    data["context"] = context;
    data["budget"] = allocation.toJson();
    return ToolResult::ok(data);
}

ToolResult handleCheckDesignRules(const QJsonObject& params) {
    QJsonArray categories = params["rule_categories"].toArray();

    // Delegate to ERC engine (available through ContextEngine path)
    QJsonArray violations;

    // Build debug suggestions as proxy for design rule violations
    QJsonArray suggestions = ContextEngine::getDebugSuggestions();
    for (const auto& s : suggestions) {
        QJsonObject sObj = s.toObject();
        QJsonObject violation;
        violation["rule"] = sObj["reason"].toString();
        violation["node"] = sObj["node"].toString();
        violation["severity"] = sObj["priority"].toInt() == 1 ? QString("error") : QString("warning");
        violation["remediation"] = QString("Review node %1: %2")
                                       .arg(sObj["node"].toString())
                                       .arg(sObj["reason"].toString());
        violations.append(violation);
    }

    QJsonObject data;
    data["violationCount"] = violations.size();
    data["violations"] = violations;
    return ToolResult::ok(data);
}

ToolResult handleGetDebugSuggestions(const QJsonObject& params) {
    QString focusArea = params["focus_area"].toString("all");

    QJsonArray suggestions = ContextEngine::getDebugSuggestions();

    // Filter by focus area if specified
    if (focusArea != "all") {
        QJsonArray filtered;
        for (const auto& s : suggestions) {
            QJsonObject sObj = s.toObject();
            QString reason = sObj["reason"].toString().toLower();

            bool include = false;
            if (focusArea == "convergence" && reason.contains("convergence")) include = true;
            if (focusArea == "bias" && reason.contains("bias")) include = true;
            if (focusArea == "signal_path" && reason.contains("signal")) include = true;
            if (focusArea == "noise" && reason.contains("noise")) include = true;

            if (include) filtered.append(s);
        }
        suggestions = filtered;
    }

    QJsonObject data;
    data["focus_area"] = focusArea;
    data["count"] = suggestions.size();
    data["suggestions"] = suggestions;
    return ToolResult::ok(data);
}

ToolResult handleSubmitFeedback(const QJsonObject& params) {
    QString queryId = params["query_id"].toString();
    int rating = params["rating"].toInt();
    QString correction = params["correction"].toString();

    FeedbackLoop::instance().submitFeedback(queryId, rating, correction);

    QJsonObject data;
    data["queryId"] = queryId;
    data["rating"] = rating;
    data["status"] = QString("recorded");
    return ToolResult::ok(data);
}

ToolResult handleGetCounterfactuals(const QJsonObject& params) {
    QString topologyHash = params["topology_hash"].toString();
    QJsonObject targetConfigJson = params["target_config"].toObject();

    TraceConfig config;
    config.solverType = targetConfigJson["solver_type"].toString("PCG");
    config.gminSteppingEnabled = targetConfigJson["gmin_stepping"].toBool(false);
    config.reltol = targetConfigJson["reltol"].toDouble(1e-3);
    config.abstol = targetConfigJson["abstol"].toDouble(1e-6);

    auto& store = getVectorStore();
    auto traces = store.findCounterfactuals(topologyHash, config);

    QJsonArray results;
    for (const auto& trace : traces) {
        QJsonObject entry;
        entry["traceId"] = trace.traceId;
        entry["isFailure"] = trace.isFailure();
        entry["solver"] = trace.configParams.solverType;
        entry["gminStepping"] = trace.configParams.gminSteppingEnabled;
        results.append(entry);
    }

    QJsonObject data;
    data["topologyHash"] = topologyHash;
    data["count"] = results.size();
    data["counterfactuals"] = results;
    return ToolResult::ok(data);
}

ToolResult handleCompressContext(const QJsonObject& params) {
    QJsonObject contextJson = params["context_json"].toObject();
    int maxTokens = params["max_tokens"].toInt(4000);
    QString modeStr = params["priority_mode"].toString("strict");

    PriorityMode mode = PriorityMode::STRICT_FILL;
    if (modeStr == "balanced") mode = PriorityMode::BALANCED;
    else if (modeStr == "debug") mode = PriorityMode::DEBUG;

    auto allocation = ContextBudgetManager::compressForBudget(contextJson, maxTokens, mode);

    // Assemble compressed content
    QString compressed;
    for (const auto& chunk : allocation.includedChunks) {
        compressed += chunk.content + "\n";
    }

    QJsonObject data;
    data["compressed_context"] = compressed;
    data["budget"] = allocation.toJson();
    return ToolResult::ok(data);
}

} // namespace ToolHandlers
} // namespace AIAPI
