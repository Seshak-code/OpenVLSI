#include "ai_assistant.h"
#include "../ai_api/context_engine.h"
#include "../ai_api/nl_parser.h"
#include "../ai_api/circuit_templates.h"
#include <QScrollBar>
#include <QDebug>
#include <QJsonDocument>
#include <QLabel>
#include <QHBoxLayout>

AiAssistantPanel::AiAssistantPanel(SchematicEditor* editor, QWidget* parent) 
    : QWidget(parent), editorContext(editor), currentDiffCard(nullptr)
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
    inputField->setPlaceholderText("Ask Copilot... (e.g., 'add a 10k resistor' or '/help')");
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
    appendMessage("Copilot", "Hello! I am your AI circuit design assistant.\n"
                             "Try natural language:\n"
                             " - 'add a 10k resistor'\n"
                             " - 'design a Darlington pair'\n"
                             " - 'run simulation'\n"
                             "Or use /commands: /dump /parts /erc /context /help");
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

    // Slash commands (explicit)
    if (cmd.startsWith("/")) {
        if (cmd == "/dump") {
            QJsonObject state = editorContext->getCircuitState();
            QJsonDocument doc(state);
            appendMessage("Copilot", "Current Scene State (JSON):\n" + doc.toJson(QJsonDocument::Indented));
        } 
        else if (cmd == "/wire") {
            QVector<QPointF> path;
            path << QPointF(0, 0) << QPointF(100, 0) << QPointF(100, 100);
            editorContext->placeWirePath(path);
            appendMessage("Copilot", "✅ Placed demo wire path!");
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
            appendMessage("Copilot", "Commands:\n"
                                     "/context - Show AI System Prompt\n"
                                     "/dump - JSON Dump\n"
                                     "/wire - (Demo) Auto Wire\n"
                                     "/parts - List Components\n"
                                     "/erc - Run ERC\n"
                                     "/templates - List circuit templates\n"
                                     "/help - This msg\n\n"
                                     "Natural Language:\n"
                                     "  'add a 1k resistor'\n"
                                     "  'design a current mirror'\n"
                                     "  'delete R5'");
        }
        else if (cmd == "/context") {
            QString ctx = AIAPI::ContextEngine::generateSystemPrompt();
            appendMessage("Copilot", "<b>AI System Context:</b>\n" + ctx);
        }
        else if (cmd == "/templates") {
            QStringList templates = AIAPI::CircuitTemplates::availableTemplates();
            appendMessage("Copilot", "Available Circuit Templates:\n - " + templates.join("\n - "));
        }
        else {
            appendMessage("Copilot", "Unknown command. Try /help for options.");
        }
    }
    else {
        // Natural language processing
        processNaturalLanguage(cmd);
    }
}

void AiAssistantPanel::processNaturalLanguage(const QString& text) {
    AIAPI::ParsedIntent intent = AIAPI::NLParser::parseIntent(text);
    
    if (intent.confidence < 0.5) {
        appendMessage("Copilot", "I couldn't understand that request. Try:\n"
                                 " - 'add a 10k resistor'\n"
                                 " - 'design a Darlington pair'\n"
                                 " - 'delete R5'\n"
                                 "Or use /help for slash commands.");
        return;
    }
    
    // Build a ChangeRequest for approval
    ChangeRequest req;
    
    if (intent.action == "placeComponent") {
        QString type = intent.params["type"].toString();
        QString value = intent.params["value"].toString();
        int x = intent.params.contains("x") ? intent.params["x"].toInt() : -1;
        int y = intent.params.contains("y") ? intent.params["y"].toInt() : -1;
        
        req.description = QString("Add %1 (%2)").arg(type).arg(value);
        if (x >= 0) req.description += QString(" at (%1, %2)").arg(x).arg(y);
        else req.description += " (auto-placed)";
        
        QJsonObject action;
        action["action"] = "placeComponent";
        action["type"] = type;
        action["value"] = value;
        action["x"] = x;
        action["y"] = y;
        req.actions.append(action);
    }
    else if (intent.action == "deleteComponent") {
        QString id = intent.params["id"].toString();
        req.description = QString("Delete component %1").arg(id);
        
        QJsonObject action;
        action["action"] = "deleteComponent";
        action["id"] = id;
        req.actions.append(action);
    }
    else if (intent.action == "instantiateTemplate") {
        QString templateName = intent.params["template"].toString();
        AIAPI::CircuitTemplate tmpl = AIAPI::CircuitTemplates::getTemplate(templateName);
        
        if (tmpl.name.isEmpty()) {
            appendMessage("Copilot", QString("Template '%1' not found. Try /templates for available options.").arg(templateName));
            return;
        }
        
        req.description = tmpl.description;
        req.templateName = templateName;
        
        // Add all component placements as actions
        for (const auto& comp : tmpl.components) {
            QJsonObject action;
            action["action"] = "placeComponent";
            action["type"] = comp.toObject()["type"].toString();
            action["id"] = comp.toObject()["id"].toString();
            action["x"] = comp.toObject()["x"].toInt();
            action["y"] = comp.toObject()["y"].toInt();
            if (comp.toObject().contains("params")) {
                action["params"] = comp.toObject()["params"].toObject();
            }
            req.actions.append(action);
        }
    }
    else if (intent.action == "runSimulation") {
        req.description = "Run DC Simulation";
        QJsonObject action;
        action["action"] = "runSimulation";
        action["type"] = "DC";
        req.actions.append(action);
    }
    else {
        appendMessage("Copilot", "Intent recognized but not yet implemented: " + intent.action);
        return;
    }
    
    // Show the diff card for approval
    showDiffCard(req);
}

void AiAssistantPanel::showDiffCard(const ChangeRequest& request) {
    // Clear any existing diff card
    clearDiffCard();
    
    pendingRequest = request;
    
    // Create the diff card widget
    currentDiffCard = new QFrame();
    currentDiffCard->setStyleSheet(
        "QFrame { background-color: #2D2D30; border: 2px solid #3E3E42; border-radius: 8px; padding: 10px; margin: 5px 0; }"
    );
    
    QVBoxLayout* cardLayout = new QVBoxLayout(currentDiffCard);
    cardLayout->setSpacing(8);
    
    // Header
    QLabel* header = new QLabel("📋 <b>Proposed Change</b>");
    header->setStyleSheet("color: #FFD700; font-size: 14px;");
    cardLayout->addWidget(header);
    
    // Description
    QLabel* desc = new QLabel(request.description);
    desc->setStyleSheet("color: #DDD; font-size: 12px;");
    desc->setWordWrap(true);
    cardLayout->addWidget(desc);
    
    // Actions list
    if (request.actions.size() > 0) {
        QString actionsHtml = "<div style='color:#888; font-size:11px;'>";
        for (const auto& action : request.actions) {
            QJsonObject obj = action.toObject();
            actionsHtml += QString("• %1: %2<br>")
                .arg(obj["action"].toString())
                .arg(obj.contains("type") ? obj["type"].toString() : obj["id"].toString());
        }
        actionsHtml += "</div>";
        QLabel* actionsLabel = new QLabel(actionsHtml);
        actionsLabel->setTextFormat(Qt::RichText);
        cardLayout->addWidget(actionsLabel);
    }
    
    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    
    QPushButton* approveBtn = new QPushButton("✅ Approve");
    approveBtn->setStyleSheet(
        "QPushButton { background-color: #28A745; color: white; border: none; padding: 6px 16px; font-weight: bold; border-radius: 4px; }"
        "QPushButton:hover { background-color: #218838; }"
    );
    connect(approveBtn, &QPushButton::clicked, this, &AiAssistantPanel::onApproveRequest);
    
    QPushButton* rejectBtn = new QPushButton("❌ Reject");
    rejectBtn->setStyleSheet(
        "QPushButton { background-color: #DC3545; color: white; border: none; padding: 6px 16px; font-weight: bold; border-radius: 4px; }"
        "QPushButton:hover { background-color: #C82333; }"
    );
    connect(rejectBtn, &QPushButton::clicked, this, &AiAssistantPanel::onRejectRequest);
    
    buttonLayout->addWidget(approveBtn);
    buttonLayout->addWidget(rejectBtn);
    buttonLayout->addStretch();
    cardLayout->addLayout(buttonLayout);
    
    // Insert the card into the layout (before the input area)
    QVBoxLayout* mainLayout = qobject_cast<QVBoxLayout*>(layout());
    if (mainLayout) {
        mainLayout->insertWidget(mainLayout->count() - 1, currentDiffCard);
    }
}

void AiAssistantPanel::onApproveRequest() {
    appendMessage("Copilot", "✅ Approved! Executing...");
    executeChangeRequest(pendingRequest);
    clearDiffCard();
}

void AiAssistantPanel::onRejectRequest() {
    appendMessage("Copilot", "❌ Rejected. No changes made.");
    clearDiffCard();
}

void AiAssistantPanel::executeChangeRequest(const ChangeRequest& request) {
    if (!editorContext) return;
    
    int successCount = 0;
    
    for (const auto& actionVal : request.actions) {
        QJsonObject action = actionVal.toObject();
        QString actionType = action["action"].toString();
        
        if (actionType == "placeComponent") {
            QString type = action["type"].toString();
            QString id = action.contains("id") ? action["id"].toString() : "";
            int x = action["x"].toInt();
            int y = action["y"].toInt();
            
            // Auto-place if no coordinates given
            if (x < 0 || y < 0) {
                QRectF bounds = editorContext->getSceneBounds();
                x = static_cast<int>(bounds.right()) + 100;
                y = static_cast<int>(bounds.center().y());
            }
            
            // Call placement API
            QString params = "";
            if (action.contains("params")) {
                QJsonDocument paramsDoc(action["params"].toObject());
                params = paramsDoc.toJson(QJsonDocument::Compact);
            }
            if (action.contains("value")) {
                params = action["value"].toString();
            }
            
            bool success = editorContext->placeComponent(type, x, y, id, params);
            if (success) successCount++;
        }
        else if (actionType == "deleteComponent") {
            QString id = action["id"].toString();
            bool success = editorContext->deleteComponent(id);
            if (success) successCount++;
        }
        else if (actionType == "runSimulation") {
            // Trigger simulation (this would connect to SimulatorWindow)
            appendMessage("Copilot", "🔬 Simulation requested. Open the Simulator window to see results.");
            successCount++;
        }
    }
    
    appendMessage("Copilot", QString("Executed %1/%2 actions successfully.")
                  .arg(successCount).arg(request.actions.size()));
}

void AiAssistantPanel::clearDiffCard() {
    if (currentDiffCard) {
        currentDiffCard->deleteLater();
        currentDiffCard = nullptr;
    }
    pendingRequest = ChangeRequest();
}
