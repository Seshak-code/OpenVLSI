#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QJsonArray>
#include <QJsonObject>
#include <QFrame>
#include "schematic_editor.h"

// ============================================================================
// AI ASSISTANT PANEL (Copilot Chat with Approval Loop)
// ============================================================================

/**
 * ChangeRequest - A proposed set of circuit modifications
 * requiring user approval before execution.
 */
struct ChangeRequest {
    QString description;      // Human-readable summary
    QJsonArray actions;       // List of { action, params } objects
    QString templateName;     // If instantiating a template
};

class AiAssistantPanel : public QWidget {
    Q_OBJECT
public:
    explicit AiAssistantPanel(SchematicEditor* editor, QWidget* parent = nullptr);
    
    // Dynamic editor context update
    void setEditor(SchematicEditor* editor) { editorContext = editor; }

private slots:
    void onSendMessage();
    void onApproveRequest();
    void onRejectRequest();
    
private:
    SchematicEditor* editorContext;
    
    QTextEdit* chatHistory;
    QLineEdit* inputField;
    QPushButton* sendButton;
    
    // Approval Loop State
    ChangeRequest pendingRequest;
    QFrame* currentDiffCard;
    QString pendingIntent_;       // Original user input for feedback
    double pendingConfidence_ = 1.0; // Parser confidence for feedback
    
    void processCommand(const QString& cmd);
    void appendMessage(const QString& sender, const QString& msg, bool isUser = false);
    
    // Approval Loop Methods
    void showDiffCard(const ChangeRequest& request);
    void executeChangeRequest(const ChangeRequest& request);
    void clearDiffCard();
    
    // NL Processing
    void processNaturalLanguage(const QString& text);
};
