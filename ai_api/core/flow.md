# AI Core Flow

The `ai_api/core/` directory is the **Action Layer** of AcuteSim's AI integration. It defines the structured contract for how the AI Assistant interacts with the workbench, from parsing raw text to instantiating complex analog topologies.

## Key Files & Responsibilities

### 1. AI Interface (`ai_interface.h`)
**Role**: The "Dispatcher". Provides a unified, JSON-based entry point for all AI actions.
-   **Command Dispatch**: Routes high-level intents (e.g., `placeComponent`, `runTransient`) to the appropriate sub-systems.
-   **Context Injection**: Provides the `getAPIDocs()` function utilized by the `PromptBuilder` to explain available actions to the LLM.

```cpp
// ai_api/core/ai_interface.h
namespace AIAPI {
    /**
     * Main command dispatcher for AI agents.
     * Accepts JSON commands and returns JSON results.
     */
    QJsonObject executeCommand(const QJsonObject& command);

    /**
     * Get API documentation for LLM context injection.
     */
    QJsonObject getAPIDocs();
}
```

### 2. Circuit Templates (`circuit_templates.h`)
**Role**: The "Topology Library". Contains curated "Golden" templates for analog building blocks.
-   **Mode Awareness**: Templates automatically adjust their components based on the current `AppMode` (e.g., using ideal sources for "Bench" vs. realistic rails for "System").
-   **Golden Status**: Defines the structural "Ground Truth" for blocks like DARLINGTON PAIRS and CURRENT MIRRORS.

```cpp
// ai_api/core/circuit_templates.h
struct CircuitTemplate {
    QString name;
    QString description;
    QJsonArray components;  // List of { type, id, x, y, params }
    QJsonArray wires;       // List of connection paths
    QStringList ports;      // External connection points
};

/**
 * Mode-aware template lookup.
 * Bench: Ideal Sources | Block: PowerPorts | System: PDN Rails
 */
static CircuitTemplate getTemplate(const QString& name, AppMode mode);
```

### 3. Natural Language Parser (`nl_parser.h`)
**Role**: The "Translator". A rule-based engine that maps flexible user language into strict API commands. This will later be changed to a stronger system into the future to support LLM integration.
-   **Regex Grounding**: Uses high-confidence patterns to extract parameters (values, coordinates, IDs).
-   **Confidence Scoring**: Provides a metric used by `RAGContextBuilder` to explain interpretations to the user.

```cpp
// ai_api/core/nl_parser.h
struct ParsedIntent {
    QString action;     // e.g., "placeComponent", "instantiateTemplate"
    QJsonObject params; // Action-specific parameters
    double confidence;  // 0.0 to 1.0 (parser grounding score)
};

/**
 * Supported pattern examples:
 * "add a 10k resistor at 100,200" -> placeComponent
 * "design a darlington pair" -> instantiateTemplate
 */
static ParsedIntent parseIntent(const QString& text);
```

## Data Flow
1.  **Parsing**: `NLParser` receives raw text and produces a `ParsedIntent`.
2.  **Dispatch**: The `executeCommand` function in `ai_interface.h` takes the intent (or a raw JSON from the LLM) and validates it.
3.  **Instantiation**: If a template is requested, `CircuitTemplates` performs a mode-aware lookup.
4.  **Application**: The final schema changes are pushed to the `gui/` layer for rendering.

## Development Rules
1.  **JSON Contract**: All AI actions MUST be representable as a valid JSON object matching the examples in `ai_interface.h`.
2.  **Mode Integrity**: When adding templates to `circuit_templates.h`, always provide variants for Bench, Block, and System modes.
3.  **Deterministic Fallback**: If `NLParser` confidence is < 0.8, the system MUST prompt the user for clarification before executing.
