#ifndef AI_ASSISTANT_PANEL_H
#define AI_ASSISTANT_PANEL_H

#include <QWidget>
#include <QTextBrowser>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QJsonObject>
#include <QJsonArray>
#include "ghost_diff_viewer.h"
#include "../../ai_api/adk/session_manager.h"
// Forward declaration
class SchematicEditor;

namespace acutesim {
namespace gui {
namespace panels {

// Struct for Schematic Actions
struct SchematicChangeRequest {
    QString description;
    QJsonArray actions;
    QString templateName;
};

class AiAssistantPanel : public QWidget {
    Q_OBJECT

public:
    explicit AiAssistantPanel(QWidget *parent = nullptr);
    
    // Set the context
    void setEditor(SchematicEditor* editor);
    
    // Add message to chat
    void appendUserMessage(const QString& msg);
    void appendAiMessage(const QString& msg);
    
    // Show a text diff suggestion (HDL/Code)
    void showCodeSuggestion(const QString& original, const QString& proposed, const QString& context);

signals:
    void querySubmitted(const QString& text);
    void simulationRequested(const QString& type);

private slots:
    void onSendClicked();
    
    // Action Approval Slots
    void onApproveSchematicChange();
    void onRejectSchematicChange();

private:
    SchematicEditor* editorContext_;
    
    QVBoxLayout* layout_;
    
    // Scrolling Chat
    QTextBrowser* chatView_;
    
    // --- Interaction Modes ---
    // 1. Ghost Diff Viewer (For Text/HDL)
    acutesim::gui::widgets::GhostDiffViewer* diffViewer_;
    
    // 2. Schematic Action Card (For Visual Changes)
    QFrame* schematicDiffCard_;
    SchematicChangeRequest pendingSchematicRequest_;
    QLabel* diffCardTitle_;
    QLabel* diffCardDesc_;
    QLabel* diffCardActions_;
    
    // Note: We might want a QStackedWidget to manage these views vs Chat
    // For now, we'll toggle visibility.
    
    // Input Area
    QPlainTextEdit* inputBox_;
    QPushButton* sendButton_;
    
    // Context Bar
    QWidget* contextBar_;
    
    // Logic Methods
    void processCommand(const QString& text);
    void processNaturalLanguage(const QString& text);
    void showSchematicDiff(const SchematicChangeRequest& req);
    void hideAllOverlays(); // Show chat
    void executeSchematicChange(const SchematicChangeRequest& req);

    // Feedback State
    QString pendingIntent_;
    double pendingConfidence_;

    // Session State (Phase 1.26)
    QString currentSessionId_;
    QString lastQueryId_;
    void ensureSession();
};

} // namespace panels
} // namespace gui
} // namespace acutesim

#endif // AI_ASSISTANT_PANEL_H
