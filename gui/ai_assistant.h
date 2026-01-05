#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include "schematic_editor.h"

// ============================================================================
// AI ASSISTANT PANEL (Copilot Chat)
// ============================================================================
class AiAssistantPanel : public QWidget {
    Q_OBJECT
public:
    explicit AiAssistantPanel(SchematicEditor* editor, QWidget* parent = nullptr);

private slots:
    void onSendMessage();
    
private:
    SchematicEditor* editorContext;
    
    QTextEdit* chatHistory;
    QLineEdit* inputField;
    QPushButton* sendButton;
    
    void processCommand(const QString& cmd);
    void appendMessage(const QString& sender, const QString& msg, bool isUser = false);
};
