# AI Context Flow

The `ai_api/context/` directory is the **Cognitive Bridge** between raw simulation data and the AI Assistant. It transforms bit-level numerical results and circuit topologies into structured, semantic prompts that ground the LLM in physical reality.

## Key Files & Responsibilities

### 1. Context Engine (`context_engine.h`)
**Role**: The "Orchestrator". Orchestrates the collection of circuit state and its transformation into AI-consumable formats.
-   **Signal Path Analysis**: Recursively traces connectivity from sources to sinks to recommend probe points.
-   **Policy Enforcement**: Implements "Raw Evidence First" logic, ensuring AI responses are prepended with factual solver logs.

```cpp
// ai_api/context/context_engine.h
/**
 * Analyzes the circuit topology to discover signal paths from sources to sinks.
 * Returns a list of paths used to ground AI in physical signal flow.
 */
static QJsonArray analyzeSignalPaths();

/**
 * Applies policy to an AI response:
 * 2. Prepends solver evidence if "Raw Evidence First" is enabled.
 */
static QString applyPolicy(const QString& aiResponse, const QJsonObject& solverEvidence);
```

### 2. RAG Context Builder (`rag_context.h`)
**Role**: The "Knowledge Architect". Manages structured snapshots of simulation runs for Retrieval-Augmented Generation (RAG).
-   **SimulationKnowledgeObject**: Self-contained snapshots (traceId, topologyHash, failures, waveform summaries).
-   **Reference Designs**: Provides few-shot learning examples (e.g., LNA, OpAmp templates).

```cpp
// ai_api/context/rag_context.h
struct SimulationKnowledgeObject {
    QString traceId;              // Unique ID for this simulation trace
    QString topologyHash;         // Hash of the circuit topology
    QJsonArray deviceMix;         // [{type: "NMOS", count: 4}, ...]
    QList<FailureLabel> failures; // Labeled failure modes grounding the AI
};
```

### 3. Prompt Builder (`prompt_builder.h`)
**Role**: The "Synthesizer". Constructs the final textual prompts sent to the LLM.

### 4. Safe I/O (`safe_io.h`)
**Role**: The "Guardrail". NASA/JPL-style utilities for reliable handling of AI data.
-   **Resource Bounding**: Enforces strict size tiers to prevent OOM/DoS.

```cpp
// ai_api/context/safe_io.h
/**
 * Data Size Tiers (NASA-style resource limits)
 */
constexpr qint64 MAX_AI_CONTEXT_SIZE        = 10 * 1024 * 1024;  // 10 MB limit
constexpr qint64 MAX_SIMULATION_TRACE_SIZE  = 250 * 1024 * 1024; // 250 MB limit
```

## Data Flow
1.  **Extraction**: `ContextEngine` pulls raw data from `TensorNetlist` and `SimulationTrace`.
2.  **Structuring**: `RAGContextBuilder` packs data into `SimulationKnowledgeObject` and compresses it to fit token limits.
3.  **Synthesis**: `PromptBuilder` merges grounding data with user intents and `AIPolicy` constraints.
4.  **Verification**: Responses are validated against the "Trust Surface" before being presented in the Approval Loop.

## Development Rules
1.  **Grounding Mandatory**: Every AI suggestion MUST be traceable to a `topologyHash` or `nodeID`.
2.  **Evidence First**: Never present AI reasoning without first displaying the supporting numerical evidence (enforced via `applyPolicy`).
3.  **NASA/JPL Discipline**: All I/O operations involving external AI data must use `SafeIo` bounded reading.
4.  **Token Efficiency**: Use functional block summarization (e.g., "LNA Stage 1") instead of raw netlists for large circuits.
