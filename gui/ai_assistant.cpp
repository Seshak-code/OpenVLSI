#include "ai_assistant.h"
#include "../ai_api/context_engine.h"
#include <QScrollBar>
#include <QDebug>
#include <QJsonDocument>

AiAssistantPanel::AiAssistantPanel(SchematicEditor* editor, QWidget* parent) 
    : QWidget(parent), editorContext(editor) 
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(5, 5, 5, 5);
    
    // History
    chatHistory = new QTextEdit();
    chatHistory->setReadOnly(true);
    chatHistory->setStyleSheet("QTextEdit { background-color: #252526; color: #DDD; border: 1px solid #3E3E42; font-family: Consolas, monospace; }");
    layout->addWidget(chatHistory);
    
    // Input Area
    QHBoxLayout* inputLayout = new QHBoxLayout();
    inputField = new QLineEdit();
    inputField->setPlaceholderText("Ask Copilot... (Try /dump or /wire)");
    inputField->setStyleSheet("QLineEdit { background-color: #3C3C3C; color: white; border: 1px solid #555; padding: 4px; }");
    
    sendButton = new QPushButton("Send");
    sendButton->setStyleSheet("QPushButton { background-color: #007ACC; color: white; border: none; padding: 5px 10px; font-weight: bold; }");
    
    inputLayout->addWidget(inputField);
    inputLayout->addWidget(sendButton);
    layout->addLayout(inputLayout);
    
    // Connections
    connect(sendButton, &QPushButton::clicked, this, &AiAssistantPanel::onSendMessage);
    connect(inputField, &QLineEdit::returnPressed, this, &AiAssistantPanel::onSendMessage);
    
    // Welcome Message
    appendMessage("Copilot", "Hello! I am your AI assistant.\nTry commands:\n - /dump : View circuit state\n - /wire : Auto-route demo\n - /help : List commands");
}

void AiAssistantPanel::onSendMessage() {
    QString text = inputField->text().trimmed();
    if (text.isEmpty()) return;
    
    appendMessage("You", text, true);
    inputField->clear();
    
    processCommand(text);
}

void AiAssistantPanel::appendMessage(const QString& sender, const QString& msg, bool isUser) {
    QString color = isUser ? "#569CD6" : "#4EC9B0"; // Blue for user, Mint for AI
    QString html = QString("<div style='margin-bottom: 5px;'><b style='color:%1;'>%2:</b> <span style='color:#DDD;'>%3</span></div>")
                   .arg(color).arg(sender).arg(msg.toHtmlEscaped().replace("\n", "<br>"));
    chatHistory->append(html);
    chatHistory->verticalScrollBar()->setValue(chatHistory->verticalScrollBar()->maximum());
}

void AiAssistantPanel::processCommand(const QString& cmd) {
    if (!editorContext) {
        appendMessage("System", "Error: No Editor Context found.");
        return;
    }

    if (cmd == "/dump") {
        QJsonObject state = editorContext->getCircuitState();
        QJsonDocument doc(state);
        appendMessage("Copilot", "Current Scene State (JSON):\n" + doc.toJson(QJsonDocument::Indented));
    } 
    else if (cmd == "/wire") {
        // Demo: Place a simple L-shaped wire
        QVector<QPointF> path;
        path << QPointF(0, 0) << QPointF(100, 0) << QPointF(100, 100);
        editorContext->placeWirePath(path);
        appendMessage("Copilot", "✅ Placed demo wire path! (This is a simple example - a real AI would analyze your circuit and route intelligently)");
    }
    else if (cmd == "/parts") {
        QStringList parts = editorContext->getAvailableComponents();
        appendMessage("Copilot", "Available Components:\n - " + parts.join("\n - "));
    }
    else if (cmd == "/erc") {
        QStringList errors = editorContext->runERC();
        if (errors.isEmpty()) {
            appendMessage("Copilot", "✅ ERC Passed! No issues found.");
        } else {
            appendMessage("Copilot", "⚠️ ERC Issues:\n - " + errors.join("\n - "));
        }
    }
    else if (cmd == "/help") {
         appendMessage("Copilot", "Commands:\n/context - Show AI System Prompt\n/dump - JSON Dump\n/wire - (Demo) Auto Wire\n/parts - List Components\n/erc - Run ERC\n/help - This msg");
    }
    else if (cmd == "/context") {
        QString ctx = AIAPI::ContextEngine::generateSystemPrompt();
        appendMessage("Copilot", "<b>AI System Context:</b>\n" + ctx);
    }
    else {
        appendMessage("Copilot", "I'm a simple mock AI. I don't understand natural language yet, but I will soon!");
    }
}
