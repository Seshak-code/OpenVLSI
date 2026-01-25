# AI System Flow

The `ai_api/system/` directory is the **Infrastructure Layer**. It provides the APIs for the AI to manipulate the editor state, report errors, and learn from user feedback.

## Key Files & Responsibilities

### 1. Schema API (`schema_api.h`)
**Role**: The "Hand". Defines the atomic actions the AI can perform on the circuit canvas.
-   **Geometric Control**: Precise component placement and routing.
-   **Collision Detection**: Ensures the AI doesn't overlap components.

```cpp
// ai_api/system/schema_api.h
struct PlacementParams {
    QString componentType;  // "Resistor", "NMOS", "Inverter"
    double x, y;            // Grid coordinates
    double rotation;        // 0, 90, 180, 270
    QJsonObject params;     // {"resistance": "10k"}
};

/**
 * Place a component at specified coordinates.
 * Returns: Component ID (unique identifier)
 */
QString placeComponent(const PlacementParams& params);
```

### 2. Crash Reporting (`crash_report.h`)
**Role**: The "Black Box". Captures detailed state when the simulation or UI crashes.
-   **Netlist Forensics**: Snapshots the node assignments and component list.
-   **Probe State**: Records exactly where probe tips were located during the crash.

```cpp
// ai_api/system/crash_report.h
struct CrashReport {
    QString timestamp;
    QString crashType;       // "MATRIX_MISMATCH", "FLOATING_NODE"
    QString errorMessage;
    
    QList<ComponentDebugInfo> components;
    QList<WireDebugInfo> wires;
    NetlistDebugInfo netlist;
};
```

### 3. Feedback Loop (`feedback_loop.h`)
**Role**: The "Nervous System". Enables Reinforcement Learning from Human Feedback (RLHF).
-   **Context Reranking**: Re-weights RAG chunks based on which ones led to "Helpful" answers.
-   **Corrections**: Stores user-supplied corrections to fine-tune future responses.

```cpp
// ai_api/system/feedback_loop.h
struct FeedbackItem {
    QString queryId;
    QString userQuery;
    int rating; // 1 (Helpful), -1 (Unhelpful)
    QString correction;
};

/**
 * Submit feedback to the RLHF engine.
 */
void submitFeedback(const QString& queryId, int rating, const QString& correction = "");
```

## Data Flow
1.  **Action**: The AI calls `placeComponent` via `schema_api.h`.
2.  **Observation**: The system returns the new `ComponentID`.
3.  **Error**: If the action causes a crash, `CrashReport` captures the stack trace and netlist.
4.  **Learning**: The user rates the interaction. `FeedbackLoop` stores this rating to adjust future confidence scores.

## Development Rules
1.  **Atomic Actions**: All schema changes must be atomic. If `placeComponent` fails halfway, it must roll back.
2.  **Privacy**: `CrashReport` data is stored locally (`~/.beaversim/crash_logs`) and only sent to the AI if the user explicitly allows it.
3.  **Feedback Persistence**: `FeedbackLoop` data must be persisted to disk to survive application restarts.
