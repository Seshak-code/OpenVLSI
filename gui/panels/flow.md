# GUI Panels Flow

The `gui/panels/` directory contains the **Conversational Interfaces** of the application. These widgets facilitate the dialogue between the User and the Agent.

## Key Files & Responsibilities

### 1. AI Assistant Panel (`ai_assistant_panel.h`)
**Role**: The Chat Window. Displays the conversation history and provides specific UI cards for reviewing AI proposals.
-   **Chat View**: Standard user/agent text stream.
-   **Review Cards**: Context-sensitive widgets (Diff Viewers, Action Buttons) that appear when the AI makes a concrete suggestion.

```cpp
struct SchematicChangeRequest {
    QString description;
    QJsonArray actions;
    QString templateName;
};

class AiAssistantPanel : public QWidget {
public:
    // Add message to chat
    void appendUserMessage(const QString& msg);
    void appendAiMessage(const QString& msg);

    // Show a text diff suggestion (HDL/Code)
    void showCodeSuggestion(const QString& original, const QString& proposed, const QString& context);

signals:
    void querySubmitted(const QString& text);
};
```

## Data Flow
1.  **Input**: User types "Add a 10k resistor" and hits Send.
2.  **Signal**: `querySubmitted("Add a 10k resistor")` is emitted.
3.  **Processing**: The `ai_api` processes the intent (handled by `WorkerManager`).
4.  **Response**: The worker returns a JSON response containing a `SchematicChangeRequest`.
5.  **Review**: The Panel renders a "Proposed Changes" card with "Approve" and "Reject" buttons.
6.  **Action**: If Approved, the actions are forwarded to `SchematicEditor::executeActions()`.

## Development Rules
1.  **Modal Awareness**: The panel must handle different types of AI output—Text, Code Diffs, and Schematic Actions—distinctly.
2.  **State Management**: It must track `pendingSchematicRequest_` to ensure that clicking "Approve" executes the correct (latest) proposal.
