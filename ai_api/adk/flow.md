# ADK Integration Layer - flow.md

## Purpose
Google ADK-compatible tool registry enabling external AI agents to consume
the C++ simulation backend as function-calling tools.

## Data Flow
```
ADK Agent Request
  -> ToolRegistry::invoke(toolName, params)
    -> validateParams() against JSON Schema
    -> ToolHandlers::<handler>(params)
      -> Delegates to: ContextEngine / LocalVectorStore / FeedbackLoop / TopologyAnalyzer / ContextBudgetManager
    <- ToolResult (success/error + JSON data)
  <- JSON response to agent
```

## Rules
1. All tools MUST have JSON Schema parameter definitions
2. Handlers MUST NOT access QGraphicsScene directly (use AiActionEnvelope)
3. Tool invocation is thread-safe (QMutex in ToolRegistry)
4. New tools require: definition in registerBuiltinTools(), handler in tool_handlers.cpp, test in test_adk_tool_registry.cpp
5. Tool names use snake_case, parameters use snake_case

## SME Checklist
- [ ] Tool manifest matches Google ADK FunctionDeclaration format
- [ ] All required parameters validated before handler dispatch
- [ ] Error responses include actionable error messages
- [ ] No direct GUI access from handlers
- [ ] Session state persisted via SafeIo (atomic writes)

## Registered Tools (8)
| Tool | Handler | Delegates To |
|------|---------|-------------|
| simulate_circuit | handleSimulateCircuit | SimulationWorker |
| retrieve_similar_designs | handleRetrieveSimilarDesigns | LocalVectorStore |
| get_circuit_context | handleGetCircuitContext | ContextEngine + ContextBudgetManager |
| check_design_rules | handleCheckDesignRules | ContextEngine (ERC proxy) |
| get_debug_suggestions | handleGetDebugSuggestions | ContextEngine |
| submit_feedback | handleSubmitFeedback | FeedbackLoop |
| get_counterfactuals | handleGetCounterfactuals | LocalVectorStore |
| compress_context | handleCompressContext | ContextBudgetManager |
