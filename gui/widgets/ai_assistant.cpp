#include "ai_assistant.h"
#include "../../ai_api/context/context_engine.h"
#include "../../ai_api/context/rag_context.h"
#include "../../ai_api/core/nl_parser.h"
#include "../../ai_api/core/circuit_templates.h"
#include "../../ai_api/system/schema_api.h"
#include "../core/mainwindow.h"
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
        appendMessage("System", "[!] No active schematic found.\nPlease create a new schematic (File > New) or select an active tab.");
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
            appendMessage("Copilot", "[OK] Placed demo wire path!");
        }
        else if (cmd == "/parts") {
            QStringList parts = editorContext->getAvailableComponents();
            appendMessage("Copilot", "Available Components:\n - " + parts.join("\n - "));
        }
        else if (cmd == "/erc") {
            QStringList errors = editorContext->runERC();
            if (errors.isEmpty()) {
                appendMessage("Copilot", "[OK] ERC Passed! No issues found.");
            } else {
                appendMessage("Copilot", "[!] ERC Issues:\n - " + errors.join("\n - "));
            }
        }
        else if (cmd == "/probe") {
            // Mode-aware probe placement for testing
            if (!editorContext) {
                appendMessage("Copilot", "[!] No active schematic. Create one first.");
                return;
            }
            
            QJsonObject state = editorContext->getCircuitState();
            QJsonArray components = state["components"].toArray();
            AppMode mode = editorContext->getMode();
            
            QPointF posTip(100, 160);  // Default fallback
            QPointF negTip(200, 320);
            
            // Mode-specific terminal detection
            for (const auto& comp : components) {
                QJsonObject c = comp.toObject();
                QString type = c["type"].toString();
                
                if (mode == AppMode::BENCH_TEST) {
                    // Bench: VoltageSource+ and Ground
                    if (type == "VoltageSource") {
                        posTip = QPointF(c["x"].toInt(), c["y"].toInt() - 40);
                    } else if (type == "Ground") {
                        negTip = QPointF(c["x"].toInt(), c["y"].toInt() - 20);
                    }
                } else if (mode == AppMode::BLOCK_SUBSYSTEM) {
                    // Block: PowerPort with specific names
                    if (type == "PowerPort") {
                        QString val = c["value"].toString();
                        if (val.contains("VCC") || val.contains("OUT")) {
                            posTip = QPointF(c["x"].toInt(), c["y"].toInt());
                        } else if (val.contains("GND") || val.contains("VSS")) {
                            negTip = QPointF(c["x"].toInt(), c["y"].toInt());
                        }
                    }
                } else if (mode == AppMode::SYSTEM_INTEGRATION) {
                    // System: RailDriver/PowerVdd and PowerVss
                    if (type == "RailDriver" || type == "PowerVdd") {
                        posTip = QPointF(c["x"].toInt(), c["y"].toInt() - 40);
                    } else if (type == "PowerVss") {
                        negTip = QPointF(c["x"].toInt(), c["y"].toInt() + 10);
                    }
                }
            }
            
            QString modeStr = (mode == AppMode::BENCH_TEST) ? "Bench" : 
                              (mode == AppMode::BLOCK_SUBSYSTEM) ? "Block" : "System";
            
            ChangeRequest req;
            req.description = QString("Place Probe (%1 Mode) - Pos: %2,%3 Neg: %4,%5")
                .arg(modeStr).arg(posTip.x()).arg(posTip.y()).arg(negTip.x()).arg(negTip.y());
            
            QJsonObject action;
            action["action"] = "placeProbe";
            action["x"] = (posTip.x() + negTip.x()) / 2;
            action["y"] = std::min(posTip.y(), negTip.y()) - 100;
            
            QJsonObject pt, nt;
            pt["x"] = posTip.x(); pt["y"] = posTip.y();
            nt["x"] = negTip.x(); nt["y"] = negTip.y();
            action["posTip"] = pt;
            action["negTip"] = nt;
            
            req.actions.append(action);
            showDiffCard(req);
        }
        else if (cmd == "/demo") {
            // Create a Demo Resistive Divider
            ChangeRequest req;
            req.description = "Create Demo Resistive Divider (5V Source + 1k Load)";
            
            // V1
            QJsonObject v1; v1["action"] = "placeComponent"; v1["type"] = "VoltageSource"; v1["x"] = 0; v1["y"] = 0; v1["id"] = "V1";
            req.actions.append(v1);
            
            // R1
            QJsonObject r1; r1["action"] = "placeComponent"; r1["type"] = "Resistor"; r1["x"] = 100; r1["y"] = 0; r1["id"] = "R1";
            r1["rotation"] = 90.0;
            req.actions.append(r1);
            
            // GND
            QJsonObject gnd; gnd["action"] = "placeComponent"; gnd["type"] = "Ground"; gnd["x"] = 100; gnd["y"] = 80; gnd["id"] = "GND";
            req.actions.append(gnd);
            
            // Wire V1 -> R1
            QJsonObject w1; w1["action"] = "placeWire"; 
            QJsonArray pts1; 
            pts1.append(QJsonArray{0, -40});  // V1 Top
            pts1.append(QJsonArray{0, -60}); 
            pts1.append(QJsonArray{100, -60}); 
            pts1.append(QJsonArray{100, -40}); // R1 Top
            w1["points"] = pts1;
            req.actions.append(w1);
            
            // Wire R1 -> GND
            QJsonObject w2; w2["action"] = "placeWire"; 
            QJsonArray pts2; 
            pts2.append(QJsonArray{100, 40}); // R1 Bot
            pts2.append(QJsonArray{100, 60}); // GND Terminal
            w2["points"] = pts2;
            req.actions.append(w2);
            
             // Wire V1 Bottom -> GND
            QJsonObject w3; w3["action"] = "placeWire"; 
            QJsonArray pts3; 
            pts3.append(QJsonArray{0, 40}); // V1 Bot
            pts3.append(QJsonArray{0, 60}); 
            pts3.append(QJsonArray{100, 60}); // Connects to GND Node
            w3["points"] = pts3;
            req.actions.append(w3);

            showDiffCard(req);
        }
        else if (cmd == "/debug") {
            // Show AI debug suggestions
            QJsonArray suggestions = AIAPI::ContextEngine::getDebugSuggestions();
            
            if (suggestions.isEmpty()) {
                appendMessage("Copilot", "✅ **No issues detected.** Circuit looks good for probing!");
                return;
            }
            
            QString response = "🔍 **Debug Suggestions** (AI-recommended probe points):\n\n";
            for (const auto& sugVal : suggestions) {
                QJsonObject sug = sugVal.toObject();
                int priority = sug["priority"].toInt();
                QString icon = (priority == 1) ? "🔴" : ((priority == 2) ? "🟡" : "🔵");
                QString node = sug["node"].toString();
                QString reason = sug["reason"].toString();
                
                response += QString("%1 **%2**: %3\n").arg(icon).arg(node).arg(reason);
            }
            response += "\n_Double-click a suggestion or type 'place probe at [node]' to add._";
            appendMessage("Copilot", response);
        }
        else if (cmd == "/paths") {
            // Show AI-discovered signal paths
            QJsonArray paths = AIAPI::ContextEngine::analyzeSignalPaths();
            
            if (paths.isEmpty()) {
                appendMessage("Copilot", "[!] No signal paths found. Add sources (V/I) and ground first.");
                return;
            }
            
            QString response = "📡 **Signal Path Analysis:**\n\n";
            for (const auto& pathVal : paths) {
                QJsonObject path = pathVal.toObject();
                QString src = path["source"].toString();
                QString sink = path["sink"].toString();
                
                QJsonArray pathComps = path["path"].toArray();
                QStringList pathList;
                for (const auto& c : pathComps) pathList << c.toString();
                
                response += QString("**%1** → **%2**\n").arg(src).arg(sink);
                response += QString("  Path: %1\n").arg(pathList.join(" → "));
                
                // Show recommended probes
                QJsonArray probes = path["probePoints"].toArray();
                if (!probes.isEmpty()) {
                    response += "  📍 Probe Points: ";
                    QStringList probeNodes;
                    for (const auto& p : probes) {
                        probeNodes << p.toObject()["node"].toString();
                    }
                    response += probeNodes.join(", ") + "\n";
                }
                response += "\n";
            }
            appendMessage("Copilot", response);
        }
        else if (cmd == "/help") {
            appendMessage("Copilot", "Commands:\n"
                                     "/help - This msg\n"
                                     "/context - Show AI System Prompt\n"
                                     "/dump - JSON Dump\n"
                                     "/wire - (Demo) Auto Wire\n"
                                     "/parts - List Components\n"
                                     "/erc - Run ERC\n"
                                     "/templates - List circuit templates\n"
                                     "/demo - Build Resistive Divider\n"
                                     "/debug - AI debug suggestions (probe placement)\n"
                                     "/paths - Signal path analysis\n\n"
                                     "Natural Language:\n"
                                     "  'add a 1k resistor'\n"
                                     "  'connect V1 to R1'\n"
                                     "  'place probe'\n"
                                     "  'analyze signal paths'\n"
                                     "  'debug suggestions'\n"
                                     "  'delete R5'");
        }
        else if (cmd == "/context") {
            QString ctx = AIAPI::ContextEngine::generateSystemPrompt();
            appendMessage("Copilot", "<b>AI System Context:</b>\n" + ctx);
        }
        else if (cmd == "/templates") {
            QStringList templates = AIAPI::CircuitTemplates::availableTemplates();
            QString modeStr = "Bench";
            if (editorContext) {
                AppMode m = editorContext->getMode();
                if (m == AppMode::BLOCK_SUBSYSTEM) modeStr = "Block";
                else if (m == AppMode::SYSTEM_INTEGRATION) modeStr = "System";
            }
            appendMessage("Copilot", QString("Available Templates (Mode: %1):\n - ").arg(modeStr) + templates.join("\n - "));
        }
        else if (cmd.startsWith("/validate")) {
            // Check mode
            if (!editorContext || editorContext->getMode() != AppMode::BLOCK_SUBSYSTEM) {
                appendMessage("Copilot", "[!] Validation is primarily for Block Mode subsystems. Switch mode to Block Subsystem checking.");
            }
            
            // Extract JSON args if present
            QJsonObject stimuli;
            QString jsonStr = cmd.mid(9).trimmed();
            if (!jsonStr.isEmpty()) {
                QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
                if (!doc.isNull() && doc.isObject()) {
                    stimuli = doc.object();
                } else {
                     appendMessage("Copilot", "[!] Invalid JSON stimuli format. Running with defaults.");
                }
            }
            
            // Attempt auto-validation
            // For now, we take the WHOLE current schematic as the "Component" to validate
            // In reality, this would likely target a specific selected sub-block, OR treat the current open file as the block definition.
            
            // 1. Get current state as 'Definition'
            QJsonObject blockDef = editorContext->getCircuitState();
            
            // 2. Auto-detect ports if no stimuli provided
            if (stimuli.isEmpty()) {
                QJsonArray autoStim;
                QJsonArray components = blockDef["components"].toArray();
                for(const auto& cVal : components) {
                    QJsonObject c = cVal.toObject();
                    if (c["type"].toString() == "PowerPort") {
                         QString name = c["name"].toString();
                         // Guess input vs output based on name
                         if (name.contains("In") || name.contains("Gate") || name.contains("VCC")) {
                             QJsonObject s;
                             s["port"] = name;
                             s["type"] = "PULSE"; // Default to pulse test
                             QJsonObject p; p["v_low"]=0; p["v_high"]=1.0;
                             s["params"] = p;
                             autoStim.append(s);
                         }
                    }
                }
                stimuli["stimuli"] = autoStim;
                appendMessage("Copilot", "Running validation with auto-detected port stimuli...");
            }
            
            // 3. Call API
            // We pass "CurrentCanvas" as ID implies we validate what's currently open
            QJsonObject result = AIAPI::validateComponent("CurrentCanvas", stimuli);
            
            if (result["passed"].toBool()) {
                 appendMessage("Copilot", "[OK] **Validation Passed**\n" + result["report"].toString());
            } else {
                 appendMessage("Copilot", "[X] **Validation Failed**\n" + result["report"].toString());
            }

            // Visible Harness Creation
            if (result.contains("harnessState")) {
                if (editorContext) {
                    if (MainWindow* mw = qobject_cast<MainWindow*>(editorContext->parent())) {
                        mw->openSchematicTab("Auto-Harness", result["harnessState"].toObject());
                        appendMessage("Copilot", "[Lab] **Test Harness Created**: Check the new 'Auto-Harness' tab.");
                    }
                }
            }
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

    // === MANUAL PARSING FOR ROBUST "CONNECT" COMMANDS ===
    if (text.startsWith("connect", Qt::CaseInsensitive)) {
        QStringList parts = text.split(" ", Qt::SkipEmptyParts);
        if (parts.size() >= 4 && parts[2] == "to") {
            QString id1 = parts[1];
            QString id2 = parts[3];
            
            QVector<QPointF> t1 = editorContext->getComponentTerminals(id1);
            QVector<QPointF> t2 = editorContext->getComponentTerminals(id2);
            
            if (!t1.isEmpty() && !t2.isEmpty()) {
                // Find closest terminal pair
                QPointF p1 = t1[0];
                QPointF p2 = t2[0];
                double minDist = 1e9;
                
                for(const auto& pt1 : t1) {
                    for(const auto& pt2 : t2) {
                        double d = QLineF(pt1, pt2).length();
                        if (d < minDist) {
                            minDist = d;
                            p1 = pt1;
                            p2 = pt2;
                        }
                    }
                }
                
                // Route L-Shape
                QJsonArray points;
                points.append(QJsonArray{p1.x(), p1.y()});
                
                // Midpoint logic (Simple)
                if (std::abs(p1.x() - p2.x()) > std::abs(p1.y() - p2.y())) {
                    // Horizontal first
                     points.append(QJsonArray{p2.x(), p1.y()});
                } else {
                    // Vertical first
                     points.append(QJsonArray{p1.x(), p2.y()});
                }
                points.append(QJsonArray{p2.x(), p2.y()});
                
                ChangeRequest req;
                req.description = QString("Connect %1 to %2").arg(id1).arg(id2);
                
                QJsonObject action;
                action["action"] = "placeWire";
                action["points"] = points;
                req.actions.append(action);
                
                showDiffCard(req);
                return;
            } else {
                appendMessage("Copilot", QString("[!] Could not find terminals for %1 or %2").arg(id1).arg(id2));
                return;
            }
        }
    }
    // ====================================================

    AIAPI::ParsedIntent intent = AIAPI::NLParser::parseIntent(text);
    
    // Store for feedback recording
    pendingIntent_ = text;
    pendingConfidence_ = intent.confidence;
    
    // === NL-COMMAND VISUAL FEEDBACK ===
    // Display recognized intent with confidence badge
    QString confidenceColor = (intent.confidence >= 0.8) ? "#28A745" : 
                              (intent.confidence >= 0.5) ? "#FFC107" : "#DC3545";
    QString confidenceBadge = QString("<span style='background-color:%1; color:white; padding:2px 6px; border-radius:3px; font-size:10px;'>%2%</span>")
                              .arg(confidenceColor).arg(static_cast<int>(intent.confidence * 100));
    
    if (intent.confidence >= 0.5) {
        // Generate confidence explanation
        AIAPI::ConfidenceExplanation explanation = AIAPI::RAGContextBuilder::explainConfidence(
            text, intent.action, intent.confidence);
        
        QString intentDisplay = QString("<div style='background:#1E1E1E; border:1px solid #3E3E42; padding:8px; margin:5px 0; border-radius:4px;'>"
                                        "<b style='color:#4EC9B0;'>Recognized Intent:</b> %1 %2<br/>"
                                        "<span style='color:#888;'>Action:</span> <code style='color:#CE9178;'>%3</code>")
                                .arg(intent.action).arg(confidenceBadge).arg(intent.action);
        
        // Add confidence reasons if < 100%
        if (intent.confidence < 1.0 && !explanation.reasons.isEmpty()) {
            intentDisplay += QString("<br/><span style='color:#888; font-size:11px;'>Why not 100%: %1</span>")
                            .arg(explanation.reasons.join(", "));
        }
        
        intentDisplay += "</div>";
        chatHistory->append(intentDisplay);
    }
    // =====================================
    
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
        
        // === SMART PROBE PLACEMENT (Pathing Logic) ===
        if (type == "Probe" && x == -1) {
            QJsonArray paths = AIAPI::ContextEngine::analyzeSignalPaths();
            if (!paths.isEmpty()) {
                QJsonObject path = paths[0].toObject();
                QString src = path["source"].toString();
                QString sink = path["sink"].toString();
                
                QPointF p1 = editorContext->findNodeCenter(src);
                QPointF p2 = editorContext->findNodeCenter(sink);
                
                // If we found valid coordinates for signal path endpoints
                if (!p1.isNull() && !p2.isNull()) {
                    req.description = QString("Place Smart Probe across Signal Path (%1 -> %2)").arg(src).arg(sink);
                    
                    QJsonObject action;
                    action["action"] = "placeProbe";
                    // Body centered above
                    action["x"] = (p1.x() + p2.x()) / 2;
                    action["y"] = std::min(p1.y(), p2.y()) - 140; 
                    
                    QJsonObject pt, nt;
                    pt["x"] = p1.x(); pt["y"] = p1.y();
                    nt["x"] = p2.x(); nt["y"] = p2.y();
                    
                    action["posTip"] = pt;
                    action["negTip"] = nt;
                    
                    req.actions.append(action);
                    showDiffCard(req);
                    return;
                }
            }
        }
        // =============================================
        
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
        
        // Mode-aware template lookup
        AppMode currentMode = editorContext ? editorContext->getMode() : AppMode::BENCH_TEST;
        AIAPI::CircuitTemplate tmpl = AIAPI::CircuitTemplates::getTemplate(templateName, currentMode);
        
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
            if (comp.toObject().contains("value")) {
                action["value"] = comp.toObject()["value"].toString();
            }
            if (comp.toObject().contains("rotation")) {
                action["rotation"] = comp.toObject()["rotation"].toDouble();
            }
            if (comp.toObject().contains("flipped")) {
                action["flipped"] = comp.toObject()["flipped"].toBool();
            }
            req.actions.append(action);
        }
        
        // Add wire placements from template
        for (const auto& wire : tmpl.wires) {
            QJsonObject wireObj = wire.toObject();
            if (wireObj.contains("points")) {
                QJsonObject action;
                action["action"] = "placeWire";
                action["points"] = wireObj["points"];
                req.actions.append(action);
            }
        }
    }
    else if (intent.action == "runSimulation") {
        req.description = "Run DC Simulation";
        QJsonObject action;
        action["action"] = "runSimulation";
        action["type"] = "DC";
        req.actions.append(action);
    }
    else if (intent.action == "analyzeSignalPath") {
        // Call ContextEngine to analyze signal paths
        QJsonArray paths = AIAPI::ContextEngine::analyzeSignalPaths();
        
        if (paths.isEmpty()) {
            appendMessage("Copilot", "[!] No signal paths found. Make sure you have sources (V/I) and a ground.");
            return;
        }
        
        QString response = "[Signal] **Signal Path Analysis:**\n\n";
        for (const auto& pathVal : paths) {
            QJsonObject path = pathVal.toObject();
            response += QString("**Source:** %1 → **Sink:** %2\n").arg(path["source"].toString()).arg(path["sink"].toString());
            
            QJsonArray pathComps = path["path"].toArray();
            QStringList pathList;
            for (const auto& c : pathComps) pathList << c.toString();
            response += QString("**Path:** %1\n\n").arg(pathList.join(" → "));
            
            response += "**Recommended Probes:**\n";
            QJsonArray probes = path["probePoints"].toArray();
            for (const auto& probeVal : probes) {
                QJsonObject probe = probeVal.toObject();
                response += QString("  • **%1**: %2\n").arg(probe["label"].toString()).arg(probe["node"].toString());
            }
        }
        
        appendMessage("Copilot", response);
        return; // No approval needed for analysis
    }
    else if (intent.action == "debugSuggestions") {
        // Call ContextEngine for debug suggestions
        QJsonArray suggestions = AIAPI::ContextEngine::getDebugSuggestions();
        
        if (suggestions.isEmpty()) {
            appendMessage("Copilot", "[OK] No issues detected. Circuit looks good!");
            return;
        }
        
        QString response = "[Search] **Debug Suggestions:**\n\n";
        for (const auto& sugVal : suggestions) {
            QJsonObject sug = sugVal.toObject();
            int priority = sug["priority"].toInt();
            QString icon = (priority == 1) ? "[High]" : ((priority == 2) ? "[Med]" : "[Low]");
            response += QString("%1 **%2**\n   %3\n\n").arg(icon).arg(sug["node"].toString()).arg(sug["reason"].toString());
        }
        
        appendMessage("Copilot", response);
        return; // No approval needed for suggestions
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
    QLabel* header = new QLabel("[Plan] <b>Proposed Change</b>");
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
    
    QPushButton* approveBtn = new QPushButton("[OK] Approve");
    approveBtn->setStyleSheet(
        "QPushButton { background-color: #28A745; color: white; border: none; padding: 6px 16px; font-weight: bold; border-radius: 4px; }"
        "QPushButton:hover { background-color: #218838; }"
    );
    connect(approveBtn, &QPushButton::clicked, this, &AiAssistantPanel::onApproveRequest);
    
    QPushButton* rejectBtn = new QPushButton("[X] Reject");
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
    appendMessage("Copilot", "[OK] Approved! Executing...");
    
    // Record feedback for learning
    AIAPI::UserFeedback fb;
    fb.originalIntent = pendingIntent_;
    fb.parsedAction = pendingRequest.description;
    fb.confidence = pendingConfidence_;
    fb.wasApproved = true;
    fb.timestamp = QDateTime::currentDateTime();
    AIAPI::FeedbackStore::recordFeedback(fb);
    
    executeChangeRequest(pendingRequest);
    clearDiffCard();
}

void AiAssistantPanel::onRejectRequest() {
    appendMessage("Copilot", "[X] Rejected. No changes made.");
    
    // Record feedback for learning
    AIAPI::UserFeedback fb;
    fb.originalIntent = pendingIntent_;
    fb.parsedAction = pendingRequest.description;
    fb.confidence = pendingConfidence_;
    fb.wasApproved = false;
    fb.timestamp = QDateTime::currentDateTime();
    AIAPI::FeedbackStore::recordFeedback(fb);
    
    clearDiffCard();
}

void AiAssistantPanel::executeChangeRequest(const ChangeRequest& request) {
    if (!editorContext) return;
    
    int successCount = 0;
    
    // ====== SPATIAL AWARENESS: Compute Template Offset ======
    // If this is a multi-component placement, check if origin is clear.
    // If not, find clear space and offset all coordinates.
    
    QPoint offset(0, 0);
    
    if (request.actions.size() > 1) {
        // Calculate bounding box of all template coordinates
        int minX = INT_MAX, minY = INT_MAX;
        int maxX = INT_MIN, maxY = INT_MIN;
        
        for (const auto& actionVal : request.actions) {
            QJsonObject action = actionVal.toObject();
            if (action["action"].toString() == "placeComponent") {
                int x = action["x"].toInt();
                int y = action["y"].toInt();
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
        
        if (minX != INT_MAX) {
            // Template bounding box size (with padding for component size)
            int templateWidth = (maxX - minX) + 100;  // 100 = component width
            int templateHeight = (maxY - minY) + 100;
            
            // Check if the template origin (minX, minY) is clear
            QPoint templateOrigin(minX, minY);
            bool originClear = !editorContext->collidesWithAny(
                templateOrigin.x() - 20, templateOrigin.y() - 20, 
                templateWidth, templateHeight
            );
            
            if (!originClear) {
                // Find clear space for this template
                QPoint clearSpot = editorContext->findClearSpace(templateWidth, templateHeight);
                
                // Offset = clearSpot - templateOrigin
                offset = QPoint(clearSpot.x() - minX, clearSpot.y() - minY);
                
                appendMessage("Copilot", 
                    QString("[Loc] Relocating template to clear space: (%1, %2)")
                        .arg(clearSpot.x()).arg(clearSpot.y()));
            }
        }
    }
    
    // ====== Execute Actions with Offset ======
    for (const auto& actionVal : request.actions) {
        QJsonObject action = actionVal.toObject();
        QString actionType = action["action"].toString();
        
        if (actionType == "placeComponent") {
            QString type = action["type"].toString();
            QString id = action.contains("id") ? action["id"].toString() : "";
            int x = action["x"].toInt() + offset.x();  // Apply offset
            int y = action["y"].toInt() + offset.y();  // Apply offset
            
            // Auto-place if no coordinates given (single component fallback)
            if (action["x"].toInt() < 0 || action["y"].toInt() < 0) {
                QRectF bounds = editorContext->getSceneBounds();
                x = static_cast<int>(bounds.right()) + 100;
                y = static_cast<int>(bounds.center().y());
            }
            
            // Call placement API
            QString params = "";
            QJsonObject paramObj;
            
            // Merge explicit params and rotation/flip into a single JSON object
            if (action.contains("params")) {
                paramObj = action["params"].toObject();
            }
            if (action.contains("value")) {
                paramObj["value"] = action["value"].toString();
            }
            if (action.contains("rotation")) {
                paramObj["rotation"] = action["rotation"].toDouble();
            }
            if (action.contains("flipped")) {
                paramObj["flipped"] = action["flipped"].toBool();
            }
            
            if (!paramObj.isEmpty()) {
                QJsonDocument paramsDoc(paramObj);
                params = paramsDoc.toJson(QJsonDocument::Compact);
            }
            
            bool success = editorContext->placeComponent(type, x, y, id, params);
            if (success) {
                successCount++;
            } else {
                appendMessage("Copilot", QString("[!] Failed to place %1 at (%2, %3). Area might be occupied.").arg(type).arg(x).arg(y));
            }
        }
        else if (actionType == "deleteComponent") {
            QString id = action["id"].toString();
            bool success = editorContext->deleteComponent(id);
            if (success) successCount++;
        }
        else if (actionType == "runSimulation") {
            // Trigger simulation (connects to SimulationWorkspace via MainWindow)
            QString analysisType = action["type"].toString();
            if (analysisType.isEmpty()) analysisType = "DC";
            emit simulationRequested(analysisType);
            appendMessage("Copilot", QString("[Lab] Starting %1 simulation...").arg(analysisType));
            successCount++;
        }
        else if (actionType == "placeProbe") {
            // Place probe with explicit tip positions
            double bx = action["x"].toInt() + offset.x();
            double by = action["y"].toInt() + offset.y();
            
            QPointF bodyPos(bx, by);
            QPointF posTip(bx, by - 60); // Default relative
            QPointF negTip(bx, by + 60); // Default relative
            
            if (action.contains("posTip")) {
                QJsonObject pt = action["posTip"].toObject();
                posTip = QPointF(pt["x"].toDouble() + offset.x(), pt["y"].toDouble() + offset.y());
            }
            if (action.contains("negTip")) {
                QJsonObject nt = action["negTip"].toObject();
                negTip = QPointF(nt["x"].toDouble() + offset.x(), nt["y"].toDouble() + offset.y());
            }

            editorContext->placeProbe("", bodyPos, posTip, negTip);
            successCount++;
        }
        else if (actionType == "placeWire") {
            // Place wire path from coordinate array
            QJsonArray pointsArray = action["points"].toArray();
            QVector<QPointF> points;
            for (const auto& pt : pointsArray) {
                QJsonArray coords = pt.toArray();
                if (coords.size() >= 2) {
                    double x = coords[0].toDouble() + offset.x();
                    double y = coords[1].toDouble() + offset.y();
                    points.append(QPointF(x, y));
                }
            }
            if (points.size() >= 2) {
                editorContext->placeWirePath(points);
                successCount++;
            }
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
