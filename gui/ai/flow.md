# GUI AI Flow

The `gui/ai/` directory manages the **User Interface State** of AI interactions. Unlike `ai_api` (which generates content), this module handles the safe display, application, and reversal of that content within the Qt context.

## Key Files & Responsibilities

### 1. The Contract (`ai_action_envelope.h`)
**Role**: The "Envelope". Prevents the AI from directly mutating the internal state. All suggestions must be wrapped in this struct, which serves as a secure courier between the AI thread and the GUI thread.
-   **Type Safety**: Ensures actions are classified (PlaceComponent, Highlight, Explain).
-   **Confidence Metadata**: Carries the score used to determine if an action requires user approval.

```cpp
// gui/ai/ai_action_envelope.h
struct AiActionEnvelope {
    enum class Type {
        PlaceComponent,   // Add new component (Ghost)
        ModifyProperty,   // Change parameter (Ghost)
        SuggestNetlist,   // Full replacement (Diff)
        Explain           // Text explanation only
    };

    QString intent;             // "Add Bias Resistor"
    QVariant payload;           // Structured data
    float confidenceScore;      // 0.0 to 1.0
};
```

### 2. History & Audit (`ai_action_history.h`)
**Role**: The "Time Machine". Manages Undo/Redo specifically for AI actions, allowing users to safely backtrack if the Copilot makes a mistake.
-   **Atomic Grouping**: Allows complex multi-step AI plans (e.g., "Synthesize Amplifier") to be treated as a single undoable block.
-   **Auditability**: Maintains a detailed log of every AI modification for review.

```cpp
// gui/ai/ai_action_history.h
class AiActionHistory : public QObject {
public:
    // Push a new action (clears redo stack)
    void pushAction(const AiActionEnvelope& action);

    // Grouping (for Multi-step suggestions)
    void beginGroup(const QString& groupName);
    void endGroup();

    AiActionEnvelope undo();
};
```

## Data Flow
1.  **Receipt**: The `ai_api` sends a JSON command.
2.  **Wrapping**: The GUI wraps it in an `AiActionEnvelope`.
3.  **Preview**: The envelope is rendered as a "Ghost" overlay (semi-transparent component).
4.  **Acceptance**: If the user approves, the action is pushed to `AiActionHistory`.
5.  **Application**: The envelope payload is executed against the `SchematicScene`.

## Development Rules
1.  **No Direct Access**: The AI logic must **never** touch `QGraphicsScene` directly; it must always go through the Envelope/History system.
2.  **Reversibility**: Every action type defined in `AiActionEnvelope` MUST have a corresponding inverse operation implemented in the Undo logic.
3.  **Thread Safety**: `AiActionEnvelope` is a value type designed to be safely passed between the AI worker thread and the main GUI thread.
