#pragma once

#include "tool_registry.h"

namespace AIAPI {

/**
 * ToolHandlers - Handler implementations for ADK builtin tools.
 *
 * Each handler delegates to existing subsystems:
 * - ContextEngine for circuit context
 * - LocalVectorStore for RAG retrieval
 * - FeedbackLoop for feedback and reranking
 * - TopologyAnalyzer for structural analysis
 * - ContextBudgetManager for token compression
 */
namespace ToolHandlers {

    /**
     * Register all handler implementations with the ToolRegistry.
     * Call once during application initialization.
     */
    void registerAll();

    // Individual handlers
    ToolResult handleSimulateCircuit(const QJsonObject& params);
    ToolResult handleRetrieveSimilarDesigns(const QJsonObject& params);
    ToolResult handleGetCircuitContext(const QJsonObject& params);
    ToolResult handleCheckDesignRules(const QJsonObject& params);
    ToolResult handleGetDebugSuggestions(const QJsonObject& params);
    ToolResult handleSubmitFeedback(const QJsonObject& params);
    ToolResult handleGetCounterfactuals(const QJsonObject& params);
    ToolResult handleCompressContext(const QJsonObject& params);

} // namespace ToolHandlers

} // namespace AIAPI
