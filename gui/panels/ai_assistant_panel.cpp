#include "ai_assistant_panel.h"
#include "../../gui/styling/theme_manager.h"
#include "schematic_editor.h"
#include "../../gui/core/gui_types.h" // For AppMode

// AI API Includes (Assuming these exist based on old file)
#include "../../ai_api/context/context_engine.h"
#include "../../ai_api/core/nl_parser.h"
#include "../../ai_api/core/circuit_templates.h"

#include <QLabel>
#include <QFrame>
#include <QDateTime>
#include <QScrollBar>
#include <QDebug>

namespace acutesim {
namespace gui {
namespace panels {

AiAssistantPanel::AiAssistantPanel(QWidget *parent) 
    : QWidget(parent), editorContext_(nullptr) 
{
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);
    
    auto& tm = acutesim::gui::styling::ThemeManager::instance();
    
    // 1. Context Chips
    contextBar_ = new QWidget(this);
    contextBar_->setFixedHeight(30);
    contextBar_->setStyleSheet(QString("background-color: %1; border-bottom: 1px solid %2;")
                               .arg(tm.getColor("ui.header").name())
                               .arg(tm.getColor("ui.border").name()));
    auto ctxLayout = new QHBoxLayout(contextBar_);
    ctxLayout->setContentsMargins(10, 0, 10, 0);
    
    QLabel* ctxLabel = new QLabel("[Active Context: None]", contextBar_);
    ctxLayout->addWidget(ctxLabel);
    ctxLayout->addStretch();
    layout_->addWidget(contextBar_);

    // 2. Chat View
    chatView_ = new QTextBrowser(this);
    chatView_->setOpenExternalLinks(false);
    chatView_->setStyleSheet(QString("background-color: %1; color: %2; border: none;")
                             .arg(tm.getColor("ui.background").name())
                             .arg(tm.getColor("ui.text").name()));
    layout_->addWidget(chatView_, 1);
    
    // 3. Ghost Diff Viewer (Code/HDL)
    diffViewer_ = new acutesim::gui::widgets::GhostDiffViewer(this);
    diffViewer_->setVisible(false);
    connect(diffViewer_, &acutesim::gui::widgets::GhostDiffViewer::accepted, this, &AiAssistantPanel::hideAllOverlays);
    connect(diffViewer_, &acutesim::gui::widgets::GhostDiffViewer::rejected, this, &AiAssistantPanel::hideAllOverlays);
    layout_->addWidget(diffViewer_, 1);
    
    // 4. Schematic Action Card (Visual Changes)
    schematicDiffCard_ = new QFrame(this);
    schematicDiffCard_->setVisible(false);
    schematicDiffCard_->setStyleSheet(QString("QFrame { background-color: %1; margin: 10px; border: 1px solid %2; border-radius: 6px; }")
                                      .arg(tm.getColor("ui.header").name())
                                      .arg(tm.getColor("ui.border").name()));
    
    QVBoxLayout* cardLayout = new QVBoxLayout(schematicDiffCard_);
    
    diffCardTitle_ = new QLabel("[Plan] Proposed Change");
    diffCardTitle_->setStyleSheet("font-weight: bold; color: #FFD700;");
    cardLayout->addWidget(diffCardTitle_);
    
    diffCardDesc_ = new QLabel();
    diffCardDesc_->setWordWrap(true);
    cardLayout->addWidget(diffCardDesc_);
    
    diffCardActions_ = new QLabel();
    diffCardActions_->setStyleSheet("color: #888; font-size: 11px;");
    cardLayout->addWidget(diffCardActions_);
    
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* approveBtn = new QPushButton("Approve");
    approveBtn->setStyleSheet("background-color: #28a745; color: white; padding: 6px; border-radius: 4px;");
    connect(approveBtn, &QPushButton::clicked, this, &AiAssistantPanel::onApproveSchematicChange);
    
    QPushButton* rejectBtn = new QPushButton("Reject");
    rejectBtn->setStyleSheet("background-color: #dc3545; color: white; padding: 6px; border-radius: 4px;");
    connect(rejectBtn, &QPushButton::clicked, this, &AiAssistantPanel::onRejectSchematicChange);
    
    btnLayout->addWidget(approveBtn);
    btnLayout->addWidget(rejectBtn);
    cardLayout->addLayout(btnLayout);
    
    layout_->addWidget(schematicDiffCard_, 0); // Don't expand vertically
    
    // 5. Input Area
    QWidget* inputContainer = new QWidget(this);
    inputContainer->setFixedHeight(60); // More compact
    inputContainer->setStyleSheet(QString("border-top: 1px solid %1; background-color: %2;")
                                  .arg(tm.getColor("ui.border").name())
                                  .arg(tm.getColor("ui.header").name()));
    auto inpLayout = new QHBoxLayout(inputContainer);
    inpLayout->setContentsMargins(5, 5, 5, 5);
    
    inputBox_ = new QPlainTextEdit(inputContainer);
    inputBox_->setPlaceholderText("Ask Copilot...");
    inputBox_->setStyleSheet(QString("border: 1px solid %1; border-radius: 4px; background-color: %2; color: %3;")
                             .arg(tm.getColor("ui.border").name())
                             .arg(tm.getColor("input.background").name())
                             .arg(tm.getColor("input.text").name()));
    
    sendButton_ = new QPushButton("Send", inputContainer);
    sendButton_->setFixedWidth(60);
    sendButton_->setStyleSheet(QString("background-color: %1; color: white; border-radius: 4px;")
                               .arg(tm.getColor("ui.accent").name()));
    
    inpLayout->addWidget(inputBox_);
    inpLayout->addWidget(sendButton_);
    
    layout_->addWidget(inputContainer);
    
    connect(sendButton_, &QPushButton::clicked, this, &AiAssistantPanel::onSendClicked);
    
    // Initialize Welcome
    appendAiMessage("Hello! I am your AI Assistant. Try 'add a 10k resistor' or 'run simulation'.");
}

void AiAssistantPanel::setEditor(SchematicEditor* editor) {
    editorContext_ = editor;
    // Update context label to show mode
    if (contextBar_->layout()->count() > 0) {
        if (QLabel* lbl = qobject_cast<QLabel*>(contextBar_->layout()->itemAt(0)->widget())) {
            if (editor) {
                QString modeStr;
                switch (editor->getMode()) {
                    case AppMode::BENCH_TEST:
                        modeStr = "BENCH";
                        break;
                    case AppMode::BLOCK_SUBSYSTEM:
                        modeStr = "BLOCK";
                        break;
                    case AppMode::SYSTEM_INTEGRATION:
                        modeStr = "SYSTEM";
                        break;
                }
                lbl->setText(QString("[Mode: %1] Active Editor").arg(modeStr));
            } else {
                lbl->setText("[No Editor Active]");
            }
        }
    }
}

void AiAssistantPanel::appendUserMessage(const QString& msg) {
    chatView_->append("<b>User:</b> " + msg.toHtmlEscaped());
    // Auto-scroll
    chatView_->verticalScrollBar()->setValue(chatView_->verticalScrollBar()->maximum());
}

void AiAssistantPanel::appendAiMessage(const QString& msg) {
    chatView_->append("<b>Copilot:</b> " + msg);
    chatView_->verticalScrollBar()->setValue(chatView_->verticalScrollBar()->maximum());
}

void AiAssistantPanel::showCodeSuggestion(const QString& original, const QString& proposed, const QString& context) {
    hideAllOverlays();
    chatView_->setVisible(false);
    diffViewer_->setVisible(true);
    diffViewer_->setDiff(original, proposed, context);
}

void AiAssistantPanel::showSchematicDiff(const SchematicChangeRequest& req) {
    hideAllOverlays();
    chatView_->setVisible(false); // Hide chat to focus on decision? Or keep it visible?
    // User probably wants to see context. Let's keep chat visible but put card ON TOP or below?
    // Current layout: Chat | Card | Input. 
    // If I keep chat visible, Card appears at bottom.
    // If I hide chat, Card takes full space.
    // Let's hide chat to enforce "Mode" switching as requested by UI design.
    chatView_->setVisible(false); 
    
    pendingSchematicRequest_ = req;
    
    diffCardTitle_->setText("[Plan] " + req.description); // Short title
    diffCardDesc_->setText(req.description);
    
    QString actionsHtml = "";
    for(const auto& val : req.actions) {
        QJsonObject obj = val.toObject();
        actionsHtml += QString("• %1: %2\n").arg(obj["action"].toString()).arg(obj["type"].toString());
    }
    diffCardActions_->setText(actionsHtml);
    
    schematicDiffCard_->setVisible(true);
}

void AiAssistantPanel::hideAllOverlays() {
    diffViewer_->setVisible(false);
    schematicDiffCard_->setVisible(false);
    chatView_->setVisible(true);
}

void AiAssistantPanel::onSendClicked() {
    QString txt = inputBox_->toPlainText().trimmed();
    if (txt.isEmpty()) return;
    
    appendUserMessage(txt);
    inputBox_->clear();
    
    processCommand(txt);
    emit querySubmitted(txt);
}

void AiAssistantPanel::processCommand(const QString& text) {
    // Basic Command/NL Router
    if (text.startsWith("/")) {
        if (text == "/help") {
            appendAiMessage("Commands: /help, /dump, /erc");
        } 
        else if (text == "/erc") {
             if (editorContext_) {
                 QStringList errs = editorContext_->runERC();
                 if (errs.isEmpty()) appendAiMessage("ERC Passed.");
                 else appendAiMessage("ERC Errors: " + errs.join(", "));
             }
        }
        else {
            appendAiMessage("Unknown command.");
        }
    } else {
        processNaturalLanguage(text);
    }
}

void AiAssistantPanel::processNaturalLanguage(const QString& text) {
    if (!editorContext_) {
        appendAiMessage("Please open a schematic first.");
        return;
    }

    // Get current mode for context-aware processing
    AppMode currentMode = editorContext_->getMode();
    QString modeContext;
    switch (currentMode) {
        case AppMode::BENCH_TEST:
            modeContext = "Bench Test mode - all sources and ideal components allowed";
            break;
        case AppMode::BLOCK_SUBSYSTEM:
            modeContext = "Block Subsystem mode - no independent sources at interface, PowerPorts only";
            break;
        case AppMode::SYSTEM_INTEGRATION:
            modeContext = "System Integration mode - RailDrivers only, explicit VDD/VSS required";
            break;
    }

    // Reuse Parser Logic (Simulated for brevity, using simple matching)
    // In production, call AIAPI::NLParser with mode context
    AIAPI::ParsedIntent intent = AIAPI::NLParser::parseIntent(text);
    pendingIntent_ = text;
    pendingConfidence_ = intent.confidence;

    // Mode-aware validation: Check if requested action is valid for current mode
    QString compType = intent.params["type"].toString().toLower();
    if (currentMode == AppMode::SYSTEM_INTEGRATION) {
        // In System mode, reject independent voltage/current sources (must use RailDriver)
        if (compType == "voltagesource" || compType == "voltage source" ||
            compType == "currentsource" || compType == "current source") {
            appendAiMessage(QString("⚠️ Cannot add '%1' in System Integration mode. Use a RailDriver for power injection instead. (%2)")
                .arg(compType).arg(modeContext));
            return;
        }
    }
    if (currentMode == AppMode::BLOCK_SUBSYSTEM) {
        // In Block mode, warn about sources at interface
        if (compType == "voltagesource" || compType == "voltage source") {
            appendAiMessage(QString("⚠️ Voltage sources should only be used internally in Block mode. Consider using PowerPort for interface. (%1)")
                .arg(modeContext));
        }
    }

    if (intent.confidence < 0.6) {
        appendAiMessage("I didn't understand. Try 'add resistor' or 'run simulation'.");
        return;
    }

    SchematicChangeRequest req;
    req.description = QString("[%1] %2: %3")
        .arg(currentMode == AppMode::BENCH_TEST ? "BENCH" :
             currentMode == AppMode::BLOCK_SUBSYSTEM ? "BLOCK" : "SYSTEM")
        .arg(intent.action)
        .arg(intent.params["type"].toString());
    
    // Construct Action Object
    QJsonObject action;
    action["action"] = intent.action;
    for(auto it = intent.params.begin(); it != intent.params.end(); ++it) {
        action[it.key()] = it.value();
    }
    
    req.actions.append(action); // Single action for now
    
    showSchematicDiff(req);
}

void AiAssistantPanel::onApproveSchematicChange() {
    executeSchematicChange(pendingSchematicRequest_);
    hideAllOverlays(); // FIX: Persistence Bug
    appendAiMessage("Action Executed: " + pendingSchematicRequest_.description);
}

void AiAssistantPanel::onRejectSchematicChange() {
    hideAllOverlays();
    appendAiMessage("Action Rejected.");
}

void AiAssistantPanel::executeSchematicChange(const SchematicChangeRequest& req) {
    if (!editorContext_) return;
    
    for(const auto& val : req.actions) {
        QJsonObject action = val.toObject();
        QString type = action["action"].toString();
        
        if (type == "placeComponent") {
            QString compType = action["type"].toString();
            int x = action["x"].toInt();
            int y = action["y"].toInt();
            double rot = action["rotation"].toDouble(0.0);
            QJsonObject params = action["params"].toObject();
            if (action.contains("value")) params["value"] = action["value"];
            
            // Use modern overload
            editorContext_->placeComponent(compType, QPointF(x, y), rot, params);
        }
        else if (type == "placeWire") {
            // Flatten points
            QVector<QPointF> path;
            QJsonArray pts = action["points"].toArray();
            for(const auto& ptVal : pts) {
                QJsonArray xy = ptVal.toArray();
                if (xy.size() >= 2) path.append(QPointF(xy[0].toDouble(), xy[1].toDouble()));
            }
            editorContext_->placeWirePath(path);
        }
        else if (type == "runSimulation") {
            emit simulationRequested(action["type"].toString());
        }
    }
}

} // namespace panels
} // namespace gui
} // namespace acutesim
