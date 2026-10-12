#include <QTimer>
#include <QToolTip>
#include <QCursor>
#include "mainwindow.h"
#include "welcome_widget.h"
#include "layout_manager.h"
#include "docking_manager.h"
#include "activity_bar.h"
#include "keybinding_manager.h"
#include "keybindings_dialog.h"
#include "icon_provider.h"
#include "storage_manager.h"
#include "../widgets/project_explorer.h"
#include "../widgets/project_model.h"
#include "../widgets/search_dock.h"
#include "../styling/theme_manager.h"
#include "../scripting/tcl_bridge.h"
#include "../orchestration/worker_manager.h"
#include "../orchestration/pdk_manager.h"
#include "../editors/schematic/schematic_editor.h"
#include "../editors/code_editor.h"
#include "../editors/json_viewer.h"
#include "../editors/image_viewer.h"
#include "../editors/hex_viewer.h"
#include "editor_manager.h"
#include "editor_factory.h"
#include <QApplication>
#include <QHeaderView>
#include <QStatusBar>
#include <QDebug>
#include <QComboBox>
#include "../widgets/inline_dialog.h"
#include <QPushButton>
#include <QLabel>
#include <QToolButton>
#include <QMenu>
#include <QShortcut>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonArray>

MainWindow::MainWindow(QWidget* parent) 
    : QMainWindow(parent),
      schematicEditor(nullptr),
      simulationWorkspace(nullptr),
      editorManager(nullptr),
      welcomeWidget(nullptr)
{
    setWindowTitle("AcuteSim - ASIC Design Suite");
    resize(1280, 720);
    
    // NASA Principle: Enable dock nesting for flexible workspace layout
    // Allows up to 3-way horizontal splits of bottom panel docks (Results, Waveforms, etc.)
    setDockNestingEnabled(true);
    
    // Initialize Design Flow Manager
    flowManager = new DesignFlowManager(this);
    connect(flowManager, &DesignFlowManager::stageChanged, this, &MainWindow::onStageChanged);

    // Initialize Docking Manager (Phase 11.2)
    dockingManager = new DockingManager(this);
    
    // 1. Setup Central Area (Stage-based Workspace)
    setupCentralWidget();

    // 2. Setup Docks (Project, Properties, Messages)
    setupDocks();

    qDebug() << "[MW INIT] setupActivityBar...";
    setupActivityBar();

    qDebug() << "[MW INIT] setupMenus...";
    // 3. Setup Menus and Toolbar
    setupMenus();
    
    qDebug() << "[MW INIT] setupStageToolbar...";
    setupStageToolbar();

    qDebug() << "[MW INIT] setupCommandPalette...";
    setupCommandPalette();

    // 4. Status Bar
    statusBar()->showMessage("Ready - RTL Design Stage");
    
    // 5. Restore Layout (Phase 11.2 State Persistence)
    // Disable layout restore in WebAssembly to prevent Qt geometry engine repaint loops
#ifndef __EMSCRIPTEN__
    acutesim::gui::core::LayoutManager::instance().restoreWindowLayout("main_window", this);
#endif
    
    qDebug() << "[MW INIT] Constructor complete";
}

void MainWindow::setupCentralWidget() {
    // ==== Stage Tabs (Top-Level) ====
    stageTabs = new QTabWidget(this);
    stageTabs->setTabPosition(QTabWidget::North);
    stageTabs->setDocumentMode(true); // VS Code style flat tabs

    // Apply modern VS Code-style tab styling
    applyModernTabStyle(stageTabs);

    // --- Stage 1: RTL Design (Unified Editor View) ---
    QWidget* rtlContainer = new QWidget();
    QVBoxLayout* rtlLayout = new QVBoxLayout(rtlContainer);
    rtlLayout->setContentsMargins(0, 0, 0, 0);
    
    // Unified Editor Tabs (VS Code Style)
    editorTabs = new QTabWidget();
    editorTabs->setDocumentMode(true);
    editorTabs->setTabsClosable(true);
    editorTabs->setMovable(true);
    applyModernTabStyle(editorTabs);
    
    // Editor Manager (Controller)
    editorManager = new acutesim::gui::core::EditorManager(editorTabs, this);

    // Register built-in editor types (highest priority first)
    {
        using namespace acutesim::gui::core;
        auto& ef = EditorFactory::instance();
        ef.registerEditor({"schematic", {".sch"}, "Schematic", "schematic",
            [](const QString& p) -> IEditor* { return new SchematicEditor(p); }, 100});
        ef.registerEditor({"json", {".json"}, "JSON", "file",
            [](const QString& p) -> IEditor* { return new acutesim::gui::editors::JsonViewer(p); }, 50});
        ef.registerEditor({"image", {".png",".jpg",".jpeg",".svg",".bmp",".gif"}, "Image", "file",
            [](const QString& p) -> IEditor* { return new acutesim::gui::editors::ImageViewer(p); }, 50});
        ef.registerEditor({"hex", {".gds",".lib",".lef",".def",".bin"}, "Binary", "file",
            [](const QString& p) -> IEditor* { return new acutesim::gui::editors::HexViewer(p); }, 20});
        ef.registerEditor({"code",
            {".v",".sv",".vams",".py",".tcl",".txt",".md",".cmake",".cfg",".yaml",".yml",".h",".cpp",".c"},
            "Code", "hdl",
            [](const QString& p) -> IEditor* { return new acutesim::gui::editors::CodeEditor(p); }, 0});
    }
    
    // Welcome Widget (shown when no tabs)
    welcomeWidget = new WelcomeWidget(this);
    // Add Welcome as a "special" non-closable tab or managed by Manager? 
    // Let's manually add it for now, Manager can handle file tabs.
    editorTabs->addTab(welcomeWidget, "Welcome");
    editorTabs->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr); // No close button
    
    // Connect Welcome Widget
    connect(welcomeWidget, &WelcomeWidget::newSchematicRequested, this, [this](){
        // Create new untitled schematic
        // In future EditorManager should handle "Untitled-1.sch"
        editorManager->openFile("Untitled.sch", "Schematics");
    });

    connect(welcomeWidget, &WelcomeWidget::openRecentProjectRequested,
            this, [this](const QString& path) {
        statusBar()->showMessage("Opening: " + path, 3000);
    });
    
    // Track active editor for context
    connect(editorManager, &acutesim::gui::core::EditorManager::activeEditorChanged, 
            this, [this](acutesim::gui::core::IEditor* editor){
             // Disconnect previous editor
             if (schematicEditor) {
                 disconnect(schematicEditor, nullptr, this, nullptr);
             }
             
             auto* sch = qobject_cast<SchematicEditor*>(editor);
             if (sch) {
                 schematicEditor = sch;
                 if (simulationWorkspace) simulationWorkspace->setSchematicEditor(sch);
                 
                 // Connect Selection Signals (Fix for Bug #2)
                 connect(sch, &SchematicEditor::itemSelected, this, &MainWindow::updateProperties);
                 connect(sch, &SchematicEditor::wireSelected, this, [this](VisualWire* w){
                     activeWire = w;
                     activeComponent = nullptr; // Exclusive selection
                     // TODO: Show wire properties? For now clear panel or show basics
                     // updateProperties(nullptr); // Just clear for now
                     // Ideally we'd have updateWireProperties(w)
                 });

                 // Phase 2: Clear stale DC annotations on topology change
                 connect(sch, &SchematicEditor::topologyChanged, this, [this](size_t) {
                     if (schematicEditor && schematicEditor->getScene())
                         schematicEditor->getScene()->clearAnnotations();
                     if (viewDCAnnotationsAction)
                         viewDCAnnotationsAction->setChecked(false);
                 });
             } else {
                 // Code editor or null
                 schematicEditor = nullptr;
                 if (simulationWorkspace) simulationWorkspace->setSchematicEditor(nullptr);
             }
    });

    // Sync Simulation Context (Unified Architecture)
    connect(editorManager, &acutesim::gui::core::EditorManager::contextChanged,
            this, [this](const acutesim::orchestration::SimulationContext& ctx){
        if (simulationWorkspace) {
             simulationWorkspace->setContext(ctx);
        }
    });
    
    // "+" button to add new layout/file
    QPushButton* addPageBtn = new QPushButton("+");
    addPageBtn->setFixedWidth(30);
    addPageBtn->setToolTip("New File");
    connect(addPageBtn, &QPushButton::clicked, this, [this](){
        editorManager->openFile("Untitled.sch", "Schematics");
    });
    editorTabs->setCornerWidget(addPageBtn, Qt::TopRightCorner);
    
    rtlLayout->addWidget(editorTabs);
    stageTabs->addTab(rtlContainer, "RTL Design");
    
    // --- Stages 2-4: RTL -> GDS Pipeline ---
    rtlPipelinePanel = new acutesim::gui::orchestration::RtlPipelinePanel(this);
    stageTabs->addTab(rtlPipelinePanel, "RTL \u2192 GDS Pipeline");
    
    // Simulator Panel (popup, not a tab - triggered from menu)
    // DockingManager handles Terminal/Output/Problems panels now.
    // simulationWorkspace remains a popup or right-dock? 
    // Plan says "SimulationWorkspace with Docking System" - it's a central widget replacement or a complex dock.
    // For now, let's keep it as is, but rely on DockingManager for the bottom panel.
    simulationWorkspace = new SimulationWorkspace(this);
    // Note: schematicEditor may be nullptr at this point - will be set when first schematic is created
    if (schematicEditor) {
        simulationWorkspace->setSchematicEditor(schematicEditor);
    }
    // Note: NOT added to stageTabs - accessed via "Set Simulation" menu
    
    // Connect simulation logs to bottom panels (using DockingManager widgets)
    connect(simulationWorkspace, &SimulationWorkspace::logMessage, this, [this](const QString& msg) {
        if (dockingManager && dockingManager->output()) {
            dockingManager->output()->appendText(msg);
        }
    });
    connect(simulationWorkspace, &SimulationWorkspace::streamMessage, this, [this](const QString& msg) {
        // Use output dock for streaming messages too
        if (dockingManager && dockingManager->output()) {
            dockingManager->output()->appendText("[STREAM] " + msg);
        }
    });
    connect(simulationWorkspace, &SimulationWorkspace::resultsReady, this, &MainWindow::onSimulationResults);

    // Wire engine telemetry events to the structured log panel
    if (simulationWorkspace->driverAdapter()) {
        connect(simulationWorkspace->driverAdapter(),
                &acutesim::gui::analysis::SimulationDriverAdapter::engineEvent,
                this, [this](const acutesim::compute::orchestration::EngineEventDTO& event) {
            if (engineLogPanel_) {
                engineLogPanel_->appendEvent(event);
            }
        });
    }

    // Connect RTL Pipeline Panel logs to output
    if (rtlPipelinePanel) {
        connect(rtlPipelinePanel, &acutesim::gui::orchestration::RtlPipelinePanel::logMessage,
                this, [this](const QString& msg) {
            if (dockingManager && dockingManager->output()) {
                dockingManager->output()->appendText("[Pipeline] " + msg);
            }
        });

        connect(rtlPipelinePanel, &acutesim::gui::orchestration::RtlPipelinePanel::overlayRequested,
                this, [this](const QString& defPath) {
            if (schematicEditor) {
                schematicEditor->showPhysicalOverlay(defPath);
            }
        });
    }

    // Connect solver violations to problems panel
    connect(simulationWorkspace, &SimulationWorkspace::solverViolation, this, [this](const ERC::Violation& v) {
        if (problemsPanel) {
            // Get existing violations (if any) or just append
            // ProblemsPanel::setViolations replaces the list, so we need to be careful
            // Ideally we'd have addViolation, but for now let's assume valid flow:
            // 1. Run starts -> clear()
            // 2. Solver fails -> emits violation
            // So we can just set a list containing this violation.
            // Or better, if there were static ERC errors, we want to keep them?
            // Usually simulation is blocked if static ERC errors exist, so this list starts empty or with warnings.
            // Let's retrieve current violations from the active scene if possible?
            
            QList<ERC::Violation> violations; 
            if (schematicEditor && schematicEditor->getScene()) {
                 violations = schematicEditor->getScene()->getViolations();
            }
            violations.append(v);
            problemsPanel->setViolations(violations);
            
            // Auto-focus problems tab
            if (messagesDock) {
                messagesDock->raise();
                messagesDock->show();
            }
        }
    });
    
    connect(simulationWorkspace, &SimulationWorkspace::simulationStarted, this, [this](const QString&) {
        // Clear panels before simulation run (using DockingManager widgets)
        if (dockingManager) {
            if (dockingManager->output()) dockingManager->output()->clear();
            if (dockingManager->waveform()) dockingManager->waveform()->clear();
        }
        if (problemsPanel) problemsPanel->clear();
        if (resultsTree_) resultsTree_->clear();

        // Auto-focus output dock for visibility
        if (outputDock_) {
            outputDock_->raise();
            outputDock_->show();
        }
    });
    
    setCentralWidget(stageTabs);
    
    // Connect tab changes to flow manager
    connect(stageTabs, &QTabWidget::currentChanged, this, [this](int idx) {
        if (idx == 0) flowManager->setStage(DesignStage::RTL_DESIGN);
        else if (idx == 1) flowManager->setStage(DesignStage::SYNTHESIS); // Pipeline tab covers Synthesis/PnR/SignOff
    });
    
    // Legacy connections removed

    
    // Note: schematicEditor connections are now made dynamically in the tab change handler above
    // This avoids null pointer issues since schematicEditor starts as nullptr
}



void MainWindow::setupDocks() {
    // --- Project Browser (Left) ---
    createProjectDock();
    addDockWidget(Qt::LeftDockWidgetArea, projectDock);
    projectDock->hide(); // Start hidden, controlled by Activity Bar
    projectDock->setTitleBarWidget(new QWidget()); // No Title Bar

    // --- Component Library (Left) ---
    createLibraryDock();
    addDockWidget(Qt::LeftDockWidgetArea, libraryDock);
    libraryDock->hide();
    libraryDock->setTitleBarWidget(new QWidget()); // No Title Bar

    // --- Search Dock (Left) ---
    createSearchDock();
    addDockWidget(Qt::LeftDockWidgetArea, searchDock_);
    searchDock_->hide();

    // NOTE: Do NOT tabify. We want exclusive single dock.

    // Mode sync removed - unified component library (all tabs always visible)

    // --- Properties Inspector (Right) ---
    createPropertiesDock();
    addDockWidget(Qt::RightDockWidgetArea, propertiesDock);

    // --- Copilot AI (Right, Tabbed) ---
    createAiDock();
    addDockWidget(Qt::RightDockWidgetArea, aiDock);
    tabifyDockWidget(propertiesDock, aiDock); // Tab them together by default

    // --- Simulation Workspace (Right, Tabbed with AI) ---
    createSimulationDock();
    addDockWidget(Qt::RightDockWidgetArea, simulationDock_);
    tabifyDockWidget(aiDock, simulationDock_);
    // Start with AI dock visible by default
    aiDock->raise();

    // --- Message Console, Output, Terminal (Bottom) ---
    // Delegated to DockingManager
    qDebug() << "[MW] Creating Core Docks via DockingManager...";
    dockingManager->createCoreDocks();
    
    // Initialize Tcl Bridge (Phase 6.2)
    acutesim::gui::scripting::TclBridge::instance().init();

    // Wire signals
    connect(dockingManager, &DockingManager::tabPromotionRequested, this, &MainWindow::addCentralTab);
    connect(dockingManager, &DockingManager::dockRestorationRequested, this, &MainWindow::removeCentralTab);
    
    TerminalWidget* term = dockingManager->terminal();
    if (term) {
        connect(term, &TerminalWidget::commandRequested, this, [this, term](const QString& cmd){
            if (cmd == "layout -reset") acutesim::gui::core::LayoutManager::instance().resetToFactory();
            else if (cmd.startsWith("db_") || cmd.startsWith("place_")) {
                // Route to TclBridge
                // Ensure we have the current DesignDb
                auto* editor = schematicEditor;
                if (editor && editor->getScene()) {
                    // Phase 6.2: Ensure DesignDb is synced with latest spatial state
                    editor->getScene()->generateNetlist();
                    acutesim::gui::scripting::TclBridge::instance().setDesignDb(&(editor->getScene()->getDesignDb()));
                    acutesim::gui::scripting::TclBridge::instance().setSchematicEditor(editor);
                }
                
                std::string result = acutesim::gui::scripting::TclBridge::instance().execute(cmd.toStdString());
                term->appendOutput(QString::fromStdString(result));
            }
        });
    }

    qDebug() << "[MW] Retrieving Docks from DockingManager...";
    messagesDock = dockingManager->getDock("analysis"); // Contains Problems + Debug panels
    outputDock_ = dockingManager->getDock("logs");      // Contains Results + Output
    waveformDock_ = dockingManager->getDock("waveforms");

    // Get widgets from DockingManager (no longer creating legacy docks)
    problemsPanel = dockingManager->problems();
    resultsTree_ = dockingManager->results();

    // Note: streamingDock_ and resultsDock_ were removed (legacy, never used)
    // DockingManager handles all bottom panel docks
    
    // Worker Orchestration Dock
    qDebug() << "[MW] Creating Worker Dock...";
    createWorkerDock();
    addDockWidget(Qt::BottomDockWidgetArea, workerDock);
#ifdef __EMSCRIPTEN__
    workerDock->hide(); // No gRPC worker in WASM; avoid cluttering the bottom panel
#endif

    // Engine Log Dock
    qDebug() << "[MW] Creating Engine Log Dock...";
    engineLogDock_ = new QDockWidget("Engine Log", this);
    engineLogDock_->setObjectName("EngineLogDock");
    engineLogDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
    engineLogPanel_ = new acutesim::gui::widgets::EngineLogPanel(engineLogDock_);
    engineLogDock_->setWidget(engineLogPanel_);
    addDockWidget(Qt::BottomDockWidgetArea, engineLogDock_);

    // Engine Console Dock
    qDebug() << "[MW] Creating Engine Console Dock...";
    consoleDock_ = new QDockWidget("Console", this);
    consoleDock_->setObjectName("ConsoleDock");
    consoleDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
    engineConsole_ = new acutesim::gui::widgets::EngineConsole(consoleDock_);
    consoleDock_->setWidget(engineConsole_);
    addDockWidget(Qt::BottomDockWidgetArea, consoleDock_);

    // Wire console to driver adapter (after simulationWorkspace is created)
    if (simulationWorkspace && simulationWorkspace->driverAdapter()) {
        engineConsole_->setDriverAdapter(simulationWorkspace->driverAdapter());
    }

    // Kick off local tool detection and wire result into PdkManager
    {
        using namespace acutesim::gui::orchestration;
        auto* wm = WorkerManager::instance();
        connect(wm, &WorkerManager::toolInventoryReady, this,
            [](const ToolInventory& inv) {
                PdkManager::instance().updateFromInventory(inv.pdkRoot, inv.installedPDKs);
            });
        wm->detectLocalTools();
    }

    qDebug() << "[MW] Creating Debug Suggestions Dock...";
    createDebugSuggestionsDock();
    addDockWidget(Qt::BottomDockWidgetArea, debugSuggestionsDock);
    
    if (messagesDock && debugSuggestionsDock) {
        qDebug() << "[MW] Splitting Problems/Debug Docks...";
        splitDockWidget(messagesDock, debugSuggestionsDock, Qt::Horizontal);
    } else {
        qWarning() << "[MW WARNING] skipped splitDockWidget: messagesDock=" << messagesDock << " debugSuggestionsDock=" << debugSuggestionsDock;
    }
    
    // Verify creation (Non-fatal)
    if (!messagesDock) qWarning() << "Problems dock creation failed";
    if (!outputDock_) qWarning() << "Output dock creation failed";
    
    qDebug() << "[MW] setupDocks Complete.";
}

void MainWindow::createAiDock() {
    aiDock = new QDockWidget("Copilot Assistant", this);
    aiDock->setObjectName("AiDock");
    aiDock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);

    // VS Code style: Hide native title bar
    aiDock->setTitleBarWidget(new QWidget());
    aiDock->setFeatures(QDockWidget::NoDockWidgetFeatures); // Disable floating/closing

    aiPanel = new acutesim::gui::panels::AiAssistantPanel(this);
    if (schematicEditor) aiPanel->setEditor(schematicEditor);

    aiDock->setWidget(aiPanel);

    // Connect AI simulation requests to the simulation workspace
    connect(aiPanel, &acutesim::gui::panels::AiAssistantPanel::simulationRequested, this, &MainWindow::onAiSimulationRequested);
}

void MainWindow::createProjectDock() {
    using namespace acutesim::gui::widgets;

    projectDock = new QDockWidget("Project Browser", this);
    projectDock->setObjectName("ProjectDock");
    projectDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    // VS Code style: Hide native title bar
    projectDock->setTitleBarWidget(new QWidget());
    projectDock->setFeatures(QDockWidget::NoDockWidgetFeatures); // Disable floating/closing

    // Create the virtual project explorer
    projectExplorer_ = new ProjectExplorer(projectDock);

    // Connect signals for file operations
    connect(projectExplorer_, &ProjectExplorer::fileOpenRequested,
            this, [this](const QString& category, const QString& fileName, const QString& filePath) {
        openFileFromExplorer(category, fileName, filePath);
    });

    connect(projectExplorer_, &ProjectExplorer::newFileRequested,
            this, [this](const QString& category) {
        // Placeholder: In future, show a dialog to create new file
        statusBar()->showMessage(QString("New file in %1...").arg(category));
    });

    connect(projectExplorer_, &ProjectExplorer::deleteFileRequested,
            this, [this](const QString& category, const QString& fileName) {
        statusBar()->showMessage(QString("Delete %1 from %2").arg(fileName, category));
    });

    // Initialize demo project with actual file paths via StorageManager
    using namespace acutesim::gui::core;
    auto& storage = StorageManager::instance();

    const QString projectName = "Demo Project";
    storage.setCurrentProject(projectName);

    ProjectModel* model = projectExplorer_->model();
    model->setProjectName(projectName);

    // List all files in storage for debug
    QStringList allFiles = storage.listFiles(projectName);
    qDebug() << "[WASM Debug] Storage inventory:" << allFiles.size() << "files";
    for (const QString& f : allFiles) {
        qDebug() << "  -> " << f << "(" << storage.loadFile(projectName, f).size() << "bytes)";
    }

    // Create demo schematic file if it doesn't exist
    bool forceInit = !storage.fileExists(projectName, ".initialized_v3");
    if (!storage.fileExists(projectName, "schematics/inverter.sch") || forceInit) {
        storage.saveFile(projectName, ".initialized_v3", "true");
        
        bool loadedFromRes = false;
        QFile resFile("/resources/cells/inverter.sch"); // Emscripten preloaded path
        if (resFile.exists() && resFile.open(QIODevice::ReadOnly)) {
            QByteArray data = resFile.readAll();
            storage.saveFile(projectName, "schematics/inverter.sch", data);
            loadedFromRes = true;
            qDebug() << "[MainWindow] POPULATED storage from preloaded resources. Size:" << data.size();
        } else {
             // Fallback for desktop or other paths
             QFile resFile2(":/resources/cells/inverter.sch");
             if (resFile2.exists() && resFile2.open(QIODevice::ReadOnly)) {
                 storage.saveFile(projectName, "schematics/inverter.sch", resFile2.readAll());
                 loadedFromRes = true;
             }
        }

        if (!loadedFromRes) {
            QJsonObject demoSchematic;
            demoSchematic["components"] = QJsonArray();
            demoSchematic["wires"] = QJsonArray();
            QJsonObject meta;
            meta["version"] = 1;
            meta["name"] = "Inverter Demo (Empty)";
            demoSchematic["metadata"] = meta;
            storage.saveFile(projectName, "schematics/inverter.sch",
                             QJsonDocument(demoSchematic).toJson());
            qDebug() << "[MainWindow] Created empty demo schematic (Resources not found).";
        }
    }


    // Create demo HDL file if it doesn't exist
    if (!storage.fileExists(projectName, "hdl/counter.v")) {
        QString verilog =
            "// Demo Counter Module\n"
            "module counter(\n"
            "    input wire clk,\n"
            "    input wire rst,\n"
            "    output reg [7:0] count\n"
            ");\n"
            "\n"
            "always @(posedge clk or posedge rst) begin\n"
            "    if (rst)\n"
            "        count <= 8'b0;\n"
            "    else\n"
            "        count <= count + 1;\n"
            "end\n"
            "\n"
            "endmodule\n";
        storage.saveFile(projectName, "hdl/counter.v", verilog.toUtf8());
        qDebug() << "[MainWindow] Created demo HDL: hdl/counter.v";
    }


    // Create demo script file if it doesn't exist
    if (!storage.fileExists(projectName, "scripts/run_synth.tcl")) {
        QString tcl =
            "# Demo Synthesis Script\n"
            "# Run with: source run_synth.tcl\n"
            "\n"
            "puts \"Starting synthesis...\"\n"
            "# read_verilog counter.v\n"
            "# synth -top counter\n"
            "puts \"Synthesis complete.\"\n";
        storage.saveFile(projectName, "scripts/run_synth.tcl", tcl.toUtf8());
        qDebug() << "[MainWindow] Created demo script: scripts/run_synth.tcl";
    }

    
    // Now populate from storage
    populateProjectTree();
    projectDock->setWidget(projectExplorer_);
}

void MainWindow::populateProjectTree() {
    using namespace acutesim::gui::core;
    using namespace acutesim::gui::widgets;

    if (!projectExplorer_) return;

    auto& storage = StorageManager::instance();
    QString projectName = storage.currentProject();
    if (projectName.isEmpty()) return;

    ProjectModel* model = projectExplorer_->model();
    model->clear();
    model->setProjectName(projectName);

    // Platform specific project root
    QString projectDir = storage.getProjectDirectory(); // e.g. AppData
    QDir baseDir(projectDir);
    
    // List all files recursively
    QStringList files = storage.listFiles(projectName);

    for (const QString& relPath : files) {
        if (relPath.startsWith(".")) continue; // Skip metadata

        QFileInfo fi(relPath);
        QString fileName = fi.fileName();
        QString category = "Other";
        
        // Categorize based on path or extension
        if (relPath.contains("schematics/") || fi.suffix() == "sch") {
            category = "Schematics";
        } else if (relPath.contains("hdl/") || fi.suffix() == "v" || fi.suffix() == "sv" || fi.suffix() == "vams") {
            category = "HDL";
        } else if (relPath.contains("scripts/") || fi.suffix() == "tcl" || fi.suffix() == "py") {
            category = "Scripts";
        } else if (relPath.contains("synthesis/")) {
            category = "Synthesis";
        } else if (relPath.contains("physical/")) {
            category = "Physical Design";
        } else if (relPath.contains("verification/")) {
            category = "Verification";
        }

        // Construct full path for the editor to open
        // StorageManager stores projects in subfolders of projectDir
        // e.g. AppData/Demo Project/schematics/inverter.sch
        QString fullPath = baseDir.filePath(projectName + "/" + relPath);
        
        model->addFile(category, fileName, fullPath);
    }
}

void MainWindow::createLibraryDock() {
    libraryDock = new QDockWidget("Component Library", this);
    libraryDock->setObjectName("LibraryDock");
    libraryDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    // VS Code style: Hide native title bar
    libraryDock->setTitleBarWidget(new QWidget());
    libraryDock->setFeatures(QDockWidget::NoDockWidgetFeatures); // Disable floating/closing

    libraryPanel = new ComponentLibrary(this);
    
    // Connect Selection
    connect(libraryPanel, &ComponentLibrary::componentSelected, 
        this, [this](ComponentType type, const QString& name){
        if (schematicEditor) {
            schematicEditor->setTool(type, name);
        }
    });
    
    libraryDock->setWidget(libraryPanel);
}

void MainWindow::createPropertiesDock() {
    propertiesDock = new QDockWidget("Properties", this);
    propertiesDock->setObjectName("PropertiesDock");
    propertiesDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    propertiesDock->setTitleBarWidget(new QWidget());
    propertiesDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    
    // Stack for Multi-Mode Support
    propertiesStack = new QStackedWidget(propertiesDock);
    
    // Mode 1: Component Properties (Table)
    propertiesTable = new QTableWidget();
    propertiesTable->setColumnCount(2);
    propertiesTable->setHorizontalHeaderLabels({"Property", "Value"});
    propertiesTable->horizontalHeader()->setStretchLastSection(true);
    propertiesTable->verticalHeader()->setVisible(false);
    propertiesTable->setAlternatingRowColors(false);

    // High-density styling for properties table
    // High-density styling for properties table (Dynamic)
    auto updateTableTheme = [this](){
        propertiesTable->setStyleSheet(acutesim::gui::styling::ThemeManager::instance().getWidgetStyleSheet("QTableWidget"));
    };
    updateTableTheme(); // Apply immediately
    
    connect(&acutesim::gui::styling::ThemeManager::instance(), &acutesim::gui::styling::ThemeManager::themeChanged, 
            this, updateTableTheme);
    propertiesTable->verticalHeader()->setDefaultSectionSize(22); // Compact row height
    
    // Example placeholder
    propertiesTable->setRowCount(3);
    propertiesTable->setItem(0, 0, new QTableWidgetItem("Name"));
    propertiesTable->setItem(0, 1, new QTableWidgetItem("R1"));
    propertiesTable->setItem(1, 0, new QTableWidgetItem("Value"));
    propertiesTable->setItem(1, 1, new QTableWidgetItem("10k"));
    propertiesTable->setItem(2, 0, new QTableWidgetItem("Tolerance"));
    propertiesTable->setItem(2, 1, new QTableWidgetItem("5%"));

    // Connect editing signal
    connect(propertiesTable, &QTableWidget::itemChanged, this, &MainWindow::onPropertyEdited);
    
    propertiesStack->addWidget(propertiesTable);

    // Mode 2: System Inspector
    systemInspector = new SystemInspector();
    propertiesStack->addWidget(systemInspector);

    propertiesDock->setWidget(propertiesStack);
}


void MainWindow::updateProperties(VisualComponent* item) {
    if (isUpdating) return;
    isUpdating = true;
    
    activeComponent = item;
    activeWire = nullptr; // Ensure exclusive selection
    propertiesTable->clearContents();
    
    if (!item) {
        propertiesTable->setRowCount(0);
        isUpdating = false;
        return;
    }
    
    // Dynamic fields count: Name + Value + Type + Params
    const QStringList keys = item->propertyKeys();
    propertiesTable->setRowCount(3 + keys.size());
    
    // 0: Name
    propertiesTable->setItem(0, 0, new QTableWidgetItem("Name"));
    propertiesTable->setItem(0, 1, new QTableWidgetItem(item->name));
    
    // 1: Value (Context Aware Label)
    QString valueLabel = "Value";
    switch(item->type) {
        case ComponentType::Resistor: valueLabel = "Resistance"; break;
        case ComponentType::Capacitor: valueLabel = "Capacitance"; break;
        case ComponentType::Inductor: valueLabel = "Inductance"; break;
        case ComponentType::VoltageSource: valueLabel = "DC voltage (V)"; break;
        case ComponentType::CurrentSource: valueLabel = "DC current (A)"; break;
        default: break;
    }
    
    propertiesTable->setItem(1, 0, new QTableWidgetItem(valueLabel));
    propertiesTable->setItem(1, 1, new QTableWidgetItem(item->value));
    
    // 2: Type (Read only)
    propertiesTable->setItem(2, 0, new QTableWidgetItem("Type"));
    QTableWidgetItem* typeItem = new QTableWidgetItem(componentTypeToString(item->type));
    typeItem->setFlags(typeItem->flags() & ~Qt::ItemIsEditable);
    propertiesTable->setItem(2, 1, typeItem);
    
    // 3+: Dynamic Params
    int row = 3;
    for (const QString& key : keys) {
        QString val = item->params.value(key);
        
        // Label shows the unit; the param key rides along for onPropertyEdited
        const QString unit = item->paramUnit(key);
        QTableWidgetItem* keyItem = new QTableWidgetItem(unit.isEmpty() ? key : key + " (" + unit + ")");
        keyItem->setData(Qt::UserRole, key);
        keyItem->setFlags(keyItem->flags() & ~Qt::ItemIsEditable); // Keys distinct
        propertiesTable->setItem(row, 0, keyItem);
        
        // Check if we have options for this specific parameter
        if (item->paramOptions.contains(key)) {
            QComboBox* combo = new QComboBox();
            combo->addItems(item->paramOptions[key]);
            combo->setCurrentText(val);
            
            // Validate: if value is custom/not in list, add it or just set it?
            // For now, assume strict list or standard behavior.
            
            // Phase 3 Hardening: Capture item as QPointer to prevent crash on late callback
            QPointer<VisualComponent> safeItem(item);
            
            connect(combo, &QComboBox::currentTextChanged, this, [this, safeItem, key](const QString& text){
                // Safety check: ensure item is still valid
                if (safeItem && activeComponent == safeItem) {
                   safeItem->params[key] = text;
                   // The source type decides which fields exist: rebuild the form
                   // once this combo's signal has returned (rebuilding deletes it).
                   if (key == "Source type") {
                       QTimer::singleShot(0, this, [this, safeItem]() {
                           if (safeItem && activeComponent == safeItem) updateProperties(safeItem);
                       });
                   }
                }
            });
            
            propertiesTable->setCellWidget(row, 1, combo);
        } else {
            // Standard Text Edit
            QTableWidgetItem* valItem = new QTableWidgetItem(val);
            
            // Phase 3: Enforce Read-Only Model for PDK Components
            // If the user selected a "Standard Cell" (PDK Component), the Model is fixed by the library.
            // For "Primitives", the Model is empty/editable to allow transformation.
            bool isPDK = (item->type == ComponentType::PDK_NFET || 
                          item->type == ComponentType::PDK_PFET || 
                          item->type == ComponentType::PDK_Resistor || 
                          item->type == ComponentType::PDK_Capacitor ||
                          item->type == ComponentType::SubCircuit);
                          
            if (key == "Model" && isPDK) {
                valItem->setFlags(valItem->flags() & ~Qt::ItemIsEditable);
                valItem->setToolTip("Model is fixed for Standard Library Cells.");
                valItem->setForeground(QBrush(QColor("#808080"))); // Gray text
            }
            
            propertiesTable->setItem(row, 1, valItem);
        }
        row++;
    }
    
    isUpdating = false;
}

void MainWindow::onPropertyEdited(QTableWidgetItem* item) {
    if (isUpdating) return;
    
    int row = item->row();
    int col = item->column();
    
    // Phase 3 Hardening: QPointer checks
    if (!activeComponent.isNull()) {
        // Source fields must be numbers: reject anything else and keep the old value
        const bool isSource = activeComponent->type == ComponentType::VoltageSource ||
                              activeComponent->type == ComponentType::CurrentSource;
        if (col == 1 && isSource && (row == 1 || row > 2)) {
            bool ok = true;
            parseSiValue(item->text(), &ok);
            if (!ok) {
                const QString keyCell = propertiesTable->item(row, 0)->text();
                const QString old = row == 1 ? activeComponent->value
                    : activeComponent->params.value(propertiesTable->item(row, 0)->data(Qt::UserRole).toString());
                isUpdating = true;
                item->setText(old);
                isUpdating = false;
                QToolTip::showText(QCursor::pos(),
                    QString("%1 needs a number, like 1.5, 10n or 2.2k. Kept %2.").arg(keyCell, old),
                    propertiesTable);
                return;
            }
        }
        if (col == 1) { // Value column
            if (row == 0) {
                activeComponent->name = item->text();
            } else if (row == 1) {
                activeComponent->value = item->text();
            } else if (row > 2) {
                // Dynamic Param
                QString key = propertiesTable->item(row, 0)->data(Qt::UserRole).toString();
                // Ensure accessing map is safe? Yes, QPointer<T>-> works.
                activeComponent->params[key] = item->text();
            }
        }
    } 
    else if (!activeWire.isNull()) {
        if (col == 1 && row == 0) {
            // Renaming Net
            if (schematicEditor) {
                schematicEditor->setWireName(activeWire, item->text());
            }
        }
    }
}

void MainWindow::createMessagesDock() {
    messagesDock = new QDockWidget("Problems", this);
    messagesDock->setObjectName("MessagesDock");
    messagesDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    messagesDock->setTitleBarWidget(new QWidget());
    messagesDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    
    problemsPanel = new ProblemsPanel(messagesDock);
    
    // Connect navigation signal to schematic editor
    connect(problemsPanel, &ProblemsPanel::navigateToLocation, this, [this](QPoint loc) {
        if (schematicEditor && schematicEditor->getScene()) {
            // Center view on the violation location
            schematicEditor->centerOn(loc.x(), loc.y());
            // Optionally highlight the location
            schematicEditor->getScene()->update();
        }
    });
    
    // Connect Run ERC button if schematic editor exists
    if (schematicEditor) {
        connect(problemsPanel->findChild<QPushButton*>("Run ERC"), &QPushButton::clicked, this, [this]() {
            if (schematicEditor && schematicEditor->getScene()) {
                auto* scene = qobject_cast<GridScene*>(schematicEditor->getScene());
                if (scene) {
                    scene->runERC();
                    // Get violations and update panel
                    QList<ERC::Violation> violations = scene->getViolations();
                    problemsPanel->setViolations(violations);
                }
            }
        });
    }

    messagesDock->setWidget(problemsPanel);
}

// Legacy createOutputDock/createStreamingDock/createResultsDock/createWaveformDock
// removed — DockingManager::createCoreDocks() creates all bottom panel widgets.

void MainWindow::onSimulationResults(const QJsonObject& results) {
    QString type = results["type"].toString();
    qDebug() << "[MainWindow] onSimulationResults received, type:" << type;

    // --- 1. Update Waveform Widget (Phase 11.3 - Multi-trace support) ---
    if (dockingManager) {
        if (WaveformWidget* wf = dockingManager->waveform()) {
            if (type == "TransientPoint") {
                double t = results["time"].toDouble();
                QJsonArray volts = results["voltages"].toArray();
                
                // Pass all voltages + probes as separate traces
                QVector<double> values;
                for (int i = 0; i < volts.size(); ++i) {
                    values.append(volts[i].toDouble());
                }
                
                // Phase 2.1: Integrated Probes
                if (results.contains("probes")) {
                    QJsonObject probes = results["probes"].toObject();
                    int pIdx = 0;
                    for (auto it = probes.begin(); it != probes.end(); ++it) {
                        int traceIdx = volts.size() + pIdx;
                        values.append(it.value().toDouble());
                        // Optional: Ensure name is registered (only first time)
                        if (t < 1e-12) {
                            wf->setTraceName(traceIdx, "Probe: " + it.key());
                        }
                        pIdx++;
                    }
                }
                
                if (!values.isEmpty()) {
                    wf->appendDataPoints(t, values);
                }
            } else if (type == "DC") {
                if (results.contains("corner") && results["corner"].toString() == "Nominal") {
                    wf->clear();
                }
            }
        } else {
            qWarning() << "[MainWindow] WaveformWidget is NULL!";
        }
    } else {
        qWarning() << "[MainWindow] DockingManager is NULL!";
    }

    // --- 2. Update Legacy Logs (Phase 7) ---
    if (type == "DC") {
        if (resultsTree_) {
            resultsTree_->clear();
            QJsonArray voltages = results["voltages"].toArray();
            qDebug() << "[MainWindow] Populating resultsTree with" << voltages.size() << "DC voltages";
            for (int i = 0; i < voltages.size(); ++i) {
                QTreeWidgetItem* item = new QTreeWidgetItem(resultsTree_);
                item->setText(0, QString("V(%1)").arg(i+1));
                item->setText(1, QString("%1 V").arg(voltages[i].toDouble(), 0, 'f', 6));
            }
            
            // Phase 2.1: Probes in DC results
            if (results.contains("probes")) {
                QJsonObject probes = results["probes"].toObject();
                for (auto it = probes.begin(); it != probes.end(); ++it) {
                    QTreeWidgetItem* item = new QTreeWidgetItem(resultsTree_);
                    item->setText(0, "Probe: " + it.key());
                    item->setText(1, QString("%1 V").arg(it.value().toDouble(), 0, 'f', 6));
                    item->setForeground(0, QBrush(QColor("#4ec9b0"))); // Distinguish probes
                }
            }
        }
        // Show output dock with results
    }
    
    // Phase 2: Annotate schematic with DC operating points
    if (type == "DC" && schematicEditor && schematicEditor->getScene()) {
        schematicEditor->getScene()->setDCResults(results);
        if (viewDCAnnotationsAction && !viewDCAnnotationsAction->isChecked()) {
            viewDCAnnotationsAction->setChecked(true);
        }
    }

    // Phase 2.1: Pass to Scripting Bridge
    QJsonDocument doc(results);
    acutesim::gui::scripting::TclBridge::instance().setLastResults(doc.toJson().toStdString());

    if (type == "DC") {
        // Show output dock with results
        if (outputDock_) {
            outputDock_->show();
            outputDock_->raise();
            qDebug() << "[MainWindow] Raised outputDock_ for DC results";
        }
    }
    else if (type == "AC") {
        if (resultsTree_) {
            resultsTree_->clear();
            QJsonArray frequencies = results["frequencies"].toArray();
            QJsonObject magnitudes = results["magnitudes"].toObject();
            QJsonObject phases     = results["phases"].toObject();

            for (auto it = magnitudes.begin(); it != magnitudes.end(); ++it) {
                QString nodeName = it.key();
                QJsonArray magArr   = it.value().toArray();
                QJsonArray phaseArr = phases.value(nodeName).toArray();

                QTreeWidgetItem* nodeItem = new QTreeWidgetItem(resultsTree_);
                nodeItem->setText(0, QString("AC: %1").arg(nodeName));
                nodeItem->setForeground(0, QBrush(QColor("#569cd6")));

                for (int i = 0; i < frequencies.size(); ++i) {
                    QTreeWidgetItem* row = new QTreeWidgetItem(nodeItem);
                    row->setText(0, QString("%1 Hz").arg(frequencies[i].toDouble(), 0, 'g', 4));
                    double mag = (i < magArr.size()) ? magArr[i].toDouble() : 0.0;
                    double ph  = (i < phaseArr.size()) ? phaseArr[i].toDouble() : 0.0;
                    row->setText(1, QString("Mag: %1 dB  Phase: %2 deg").arg(mag, 0, 'f', 3).arg(ph, 0, 'f', 2));
                }
            }
        }
        if (outputDock_) {
            outputDock_->show();
            outputDock_->raise();
        }
    }
    else if (type == "Noise") {
        if (resultsTree_) {
            resultsTree_->clear();
            double integratedVrms = results["integrated_vrms"].toDouble();
            QJsonArray frequencies = results["frequencies"].toArray();
            QJsonArray psd         = results["psd"].toArray();

            QTreeWidgetItem* summaryItem = new QTreeWidgetItem(resultsTree_);
            summaryItem->setText(0, "Integrated Noise");
            summaryItem->setText(1, QString("%1 V(rms)").arg(integratedVrms, 0, 'g', 4));
            summaryItem->setForeground(0, QBrush(QColor("#4ec9b0")));

            QTreeWidgetItem* psdParent = new QTreeWidgetItem(resultsTree_);
            psdParent->setText(0, "PSD (V^2/Hz)");
            for (int i = 0; i < frequencies.size(); ++i) {
                QTreeWidgetItem* row = new QTreeWidgetItem(psdParent);
                row->setText(0, QString("%1 Hz").arg(frequencies[i].toDouble(), 0, 'g', 4));
                double val = (i < psd.size()) ? psd[i].toDouble() : 0.0;
                row->setText(1, QString("%1").arg(val, 0, 'g', 4));
            }
        }
        if (outputDock_) {
            outputDock_->show();
            outputDock_->raise();
        }
    }
    else if (type == "TransientPoint") {
        // Note: Waveform data is now displayed via WaveformWidget (handled above)
        // Show waveform dock during transient analysis
        if (waveformDock_) {
            waveformDock_->show();
            waveformDock_->raise();
        }
    }
}

void MainWindow::setupMenus() {
    QMenuBar* mb = menuBar();
    
    // File
    QMenu* fileMenu = mb->addMenu("&File");
    fileMenu->addAction(QIcon::fromTheme("document-new"), "New Project (Coming Soon)")->setEnabled(false);
    fileMenu->addAction(QIcon::fromTheme("document-open"), "Open... (Coming Soon)")->setEnabled(false);
    fileMenu->addSeparator();
    fileMenu->addAction("Exit", this, &QWidget::close);

    // Edit
    QMenu* editMenu = mb->addMenu("&Edit");
    editMenu->addAction("Undo (Coming Soon)")->setEnabled(false);
    editMenu->addAction("Redo (Coming Soon)")->setEnabled(false);
    editMenu->addSeparator();
    editMenu->addAction("Keyboard Shortcuts...", this, &MainWindow::showKeybindingsDialog);

    // View
    QMenu* viewMenu = mb->addMenu("&View");
    viewMenu->addAction(projectDock->toggleViewAction());
    viewMenu->addAction(propertiesDock->toggleViewAction());
    viewMenu->addAction(aiDock->toggleViewAction());
    viewMenu->addAction(messagesDock->toggleViewAction());
    
    // Bottom Panel Docks - allow restoring closed/split panels
    if (outputDock_) viewMenu->addAction(outputDock_->toggleViewAction());
    if (waveformDock_) viewMenu->addAction(waveformDock_->toggleViewAction());
    if (debugSuggestionsDock) viewMenu->addAction(debugSuggestionsDock->toggleViewAction());
    viewMenu->addSeparator();
    
    // Quick Preview Toggle
    viewQuickPreviewAction = new QAction("Enable Quick Preview", this);
    viewQuickPreviewAction->setCheckable(true);
    viewQuickPreviewAction->setChecked(true); // Default ON
    viewMenu->addAction(viewQuickPreviewAction);
    
    connect(viewQuickPreviewAction, &QAction::toggled, this, [this](bool checked) {
        if (schematicEditor) {
            schematicEditor->setQuickPreviewEnabled(checked);
        }
    });

    // Phase 2: DC Operating Point Annotations Toggle
    viewDCAnnotationsAction = viewMenu->addAction("Show DC Annotations");
    viewDCAnnotationsAction->setCheckable(true);
    viewDCAnnotationsAction->setChecked(false);
    connect(viewDCAnnotationsAction, &QAction::toggled, this, [this](bool checked) {
        if (schematicEditor && schematicEditor->getScene())
            schematicEditor->getScene()->setAnnotationsEnabled(checked);
    });

    // Phase 7: HDL Editor Toggle removed

    viewMenu->addSeparator();

    // Phase 11.2: Layout submenu
    layoutMenu_ = viewMenu->addMenu("Layouts");
    updateLayoutMenu();

    // Connect to LayoutManager for dynamic updates
    connect(&acutesim::gui::core::LayoutManager::instance(), &acutesim::gui::core::LayoutManager::layoutChanged,
            this, &MainWindow::updateLayoutMenu);

    // Simulation
    QMenu* simMenu = mb->addMenu("&Simulation");
    simMenu->addAction("Show Simulation Panel", this, &MainWindow::runSimulation);
    simMenu->addSeparator();

    QAction* runSimAction = simMenu->addAction("Run Simulation", this, [this]() {
        if (simulationWorkspace && schematicEditor) {
            simulationWorkspace->setSchematicEditor(schematicEditor);
            simulationWorkspace->runCurrent();
            // Show the simulation dock if hidden
            if (simulationDock_ && !simulationDock_->isVisible()) {
                simulationDock_->show();
            }
        } else {
            statusBar()->showMessage("No schematic loaded for simulation", 3000);
        }
    });
    runSimAction->setShortcut(QKeySequence(Qt::Key_F5));

    QAction* stopSimAction = simMenu->addAction("Stop Simulation", this, [this]() {
        if (simulationWorkspace) {
            simulationWorkspace->stopSimulation();
        }
    });
    stopSimAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F5));

    simMenu->addSeparator();
    simMenu->addAction("Run Quick DC", this, [this]() {
        if (simulationWorkspace && schematicEditor) {
            simulationWorkspace->setSchematicEditor(schematicEditor);
            simulationWorkspace->runQuickDC();
        }
    });
}

void MainWindow::newProject() {
    // Create new schematic tab
    // TODO: Create new schematic using EditorManager
    // editorManager->openFile("untitled.sch");
    statusBar()->showMessage("New schematic created", 3000);
}

void MainWindow::openProject() {
    using namespace acutesim::gui::widgets;
    InlineDialog::getOpenFileName(this, "Open Project",
        "AcuteSim Projects (*.acu);;Schematic Files (*.sch);;All Files (*)",
        [this](const DialogResult& r) {
            if (r.clickedButton == DialogButton::Cancel || r.textValue.isEmpty()) return;
            QString filePath = r.textValue;
            if (welcomeWidget) {
                welcomeWidget->addRecentProject(filePath, QFileInfo(filePath).baseName());
            }
            statusBar()->showMessage("Project loading not yet implemented: " + filePath, 3000);
        });
}

void MainWindow::runSimulation() {
    if (!simulationDock_) return;

    // Toggle visibility of the simulation dock
    if (simulationDock_->isVisible()) {
        simulationDock_->hide();
    } else {
        simulationDock_->show();
        simulationDock_->raise();
        
        // Ensure the schematic editor link is fresh
        if (schematicEditor && simulationWorkspace) {
            simulationWorkspace->setSchematicEditor(schematicEditor);
        }
    }
}


// openSchematicTab removed - legacy

void MainWindow::addCentralTab(QWidget* widget, const QString& title) {
    if (!stageTabs || !widget) return;
    stageTabs->addTab(widget, title);
    stageTabs->setCurrentWidget(widget);
}

void MainWindow::removeCentralTab(QWidget* widget) {
    if (!stageTabs || !widget) return;
    int idx = stageTabs->indexOf(widget);
    if (idx >= 0) {
        stageTabs->removeTab(idx);
    }
}

// ============================================================================
// FILE OPENING FROM PROJECT EXPLORER
// ============================================================================

void MainWindow::openFileFromExplorer(const QString& category, const QString& fileName, const QString& filePath) {
    qDebug() << "[MainWindow] openFileFromExplorer:" << category << fileName << filePath;
    if (editorManager) {
        editorManager->openFile(filePath, category);
    }
}


// detectModeFromSchematic() removed - unified component library

void MainWindow::closeEvent(QCloseEvent *event) {
    // Save layout state (Phase 11.2)
    acutesim::gui::core::LayoutManager::instance().saveWindowLayout("main_window", this);
    QMainWindow::closeEvent(event);
}

void MainWindow::updateWireProperties(VisualWire* wire) {
    if (isUpdating) return;
    isUpdating = true;
    
    activeWire = wire;
    activeComponent = nullptr; // Exclusive selection
    propertiesTable->clearContents();
    
    if (!wire) {
        propertiesTable->setRowCount(0);
        isUpdating = false;
        return;
    }
    
    propertiesTable->setRowCount(2);
    
    // 0: Name (Editable)
    propertiesTable->setItem(0, 0, new QTableWidgetItem("Name"));
    QTableWidgetItem* valItem = new QTableWidgetItem(wire->getName());
    valItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsEnabled);
    valItem->setToolTip("Double-click to rename net");
    propertiesTable->setItem(0, 1, valItem);
    
    // 1: Length (Read Only)
    propertiesTable->setItem(1, 0, new QTableWidgetItem("Length"));
    QTableWidgetItem* lenItem = new QTableWidgetItem(QString::number(wire->line().length()));
    lenItem->setFlags(lenItem->flags() & ~Qt::ItemIsEditable);
    propertiesTable->setItem(1, 1, lenItem);
    
    isUpdating = false;
}

void MainWindow::setupStageToolbar() {
    modeToolbar = addToolBar("Design Flow");
    modeToolbar->setMovable(false);
}
    
    // Stage Selector removed - tabs serve as navigation

    // Sync Button with Dropdown Menu (Phase 14.2 - VS Code style)





void MainWindow::onStageChanged(DesignStage stage) {
    // Update Component Library based on stage
    // Update Component Library: Unified library handles all stages now (always visible)
    
    // Update status bar
    statusBar()->showMessage(QString("Stage: %1").arg(DesignFlowManager::stageName(stage)));
    
    // Update Properties Panel
    switch (stage) {
        case DesignStage::RTL_DESIGN:
        case DesignStage::SYNTHESIS:
            propertiesStack->setCurrentWidget(propertiesTable);
            propertiesDock->setWindowTitle("Component Properties");
            break;
        case DesignStage::PHYSICAL_DESIGN:
        case DesignStage::VERIFICATION:
            propertiesStack->setCurrentWidget(systemInspector);
            propertiesDock->setWindowTitle("System Inspector");
            break;
    }
}

// addRtlPage removed - handled by EditorManager
// createSchematicFromWelcome removed - handled by EditorManager
// onRtlTabChanged removed - handled by EditorManager signals

// modeLabel(), getMode(), setMode() removed - unified component library

void MainWindow::createWorkerDock() {
    workerDock = new QDockWidget("Worker Status", this);
    workerDock->setObjectName("WorkerDock");
    workerDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::RightDockWidgetArea);
    workerDock->setTitleBarWidget(new QWidget());
    workerDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    workerPanel = new acutesim::gui::orchestration::WorkerStatusPanel(workerDock);
    workerDock->setWidget(workerPanel);
}

void MainWindow::createDebugSuggestionsDock() {
    debugSuggestionsDock = new QDockWidget("AI Debug Hints", this);
    debugSuggestionsDock->setObjectName("DebugSuggestionsDock");
    debugSuggestionsDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::RightDockWidgetArea);
    debugSuggestionsDock->setTitleBarWidget(new QWidget());
    debugSuggestionsDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    
    debugSuggestionsPanel = new acutesim::gui::widgets::DebugSuggestionsPanel(debugSuggestionsDock);
    debugSuggestionsPanel->setEditor(schematicEditor); // Set initial editor
    
    debugSuggestionsDock->setWidget(debugSuggestionsPanel);
}

void MainWindow::createSimulationDock() {
    simulationDock_ = new QDockWidget("Simulation", this);
    simulationDock_->setObjectName("SimulationDock");
    simulationDock_->setAllowedAreas(Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
    simulationDock_->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);

    // simulationWorkspace is already created in setupCentralWidget
    simulationDock_->setWidget(simulationWorkspace);

    // Note: addDockWidget is called by the caller after this function
    // Start hidden - toggled via menu or shown when running simulation
    simulationDock_->hide();

    qDebug() << "[MainWindow] Simulation dock created";
}

void MainWindow::createSearchDock() {
    using namespace acutesim::gui::widgets;

    searchDock_ = new QDockWidget("Search", this);
    searchDock_->setObjectName("SearchDock");
    searchDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    // VS Code style: Hide native title bar
    searchDock_->setTitleBarWidget(new QWidget());
    searchDock_->setFeatures(QDockWidget::NoDockWidgetFeatures);

    searchPanel_ = new SearchDock(searchDock_);

    // Connect navigation signals
    connect(searchPanel_, &SearchDock::navigateToComponent, this, [this](const QString& name) {
        statusBar()->showMessage(QString("Navigated to component: %1").arg(name), 3000);
    });

    connect(searchPanel_, &SearchDock::navigateToNet, this, [this](const QString& name) {
        statusBar()->showMessage(QString("Navigated to net: %1").arg(name), 3000);
    });

    searchDock_->setWidget(searchPanel_);
}

void MainWindow::onAiSimulationRequested(const QString& analysisType) {
    // 1. Show the Simulation Dock
    if (simulationDock_) {
        simulationDock_->show();
        simulationDock_->raise();
    }
    
    // 2. Sync schematic editor if available
    if (simulationWorkspace && schematicEditor) {
        simulationWorkspace->setSchematicEditor(schematicEditor);
    }
    
    // 3. Map analysis type to profile ID and run
    if (simulationWorkspace) {
        QString profileId = "quick_dc"; // Default
        if (analysisType.compare("Transient", Qt::CaseInsensitive) == 0 || 
            analysisType.compare("TRAN", Qt::CaseInsensitive) == 0) {
            profileId = "transient_default";
        } else if (analysisType.compare("AC", Qt::CaseInsensitive) == 0) {
            profileId = "ac_sweep_default";
        }
        
        simulationWorkspace->selectProfile(profileId);
        simulationWorkspace->runCurrent();
    }
    
    statusBar()->showMessage(QString("AI-requested %1 simulation started").arg(analysisType), 4000);
}

// Slot to handle simulation results (Waveforms, etc.)

void MainWindow::setupActivityBar() {
    using namespace acutesim::gui::core;
    
    activityBar_ = new ActivityBar(this);
    addToolBar(Qt::LeftToolBarArea, activityBar_);
    
    // Add Activities
    // 1. Explorer (Project)
    activityBar_->addActivity("explorer", IconProvider::icon("explorer"), "Project Explorer");

    // 2. Components (Library) - Use chip/component icon
    activityBar_->addActivity("components", IconProvider::icon("components"), "Components");

    // 3. Search (Placeholder)
    activityBar_->addActivity("search", IconProvider::icon("search"), "Search");
    
    // Spacer to push Settings to bottom
    QWidget* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    activityBar_->addWidget(spacer);

    // 4. Settings (Bottom)
    // Note: We use addAction directly so it's not part of the exclusive sidebar group
    QAction* settingsAction = activityBar_->addAction(IconProvider::icon("settings"), "Settings");
    settingsAction->setToolTip("Settings");
    connect(settingsAction, &QAction::triggered, this, &MainWindow::showKeybindingsDialog);
    
    // Track current sidebar state
    static QString currentSidebarId;

    // Connect Logic - VS Code style toggle behavior
    connect(activityBar_, &ActivityBar::activityChanged, this, [this](const QString& id){
        // Empty ID means "toggle off" - hide all sidebars
        if (id.isEmpty()) {
            if (projectDock) projectDock->hide();
            if (libraryDock) libraryDock->hide();
            if (searchDock_) searchDock_->hide();
            currentSidebarId.clear();
            return;
        }

        // Only hide docks that aren't the target (avoids unnecessary hide+show
        // cycles which cause WASM QBackingStore to lose backing for sibling widgets)
        if (id != "explorer" && projectDock) projectDock->hide();
        if (id != "components" && libraryDock) libraryDock->hide();
        if (id != "search" && searchDock_) searchDock_->hide();

        // Show the selected sidebar
        if (id == "explorer" && projectDock) {
            projectDock->show();
        }
        else if (id == "components" && libraryDock) {
            libraryDock->show();
        }
        else if (id == "search" && searchDock_) {
            searchDock_->show();
            if (searchPanel_ && schematicEditor) {
                searchPanel_->setSchematicEditor(schematicEditor);
            }
        }

        currentSidebarId = id;

#ifdef __EMSCRIPTEN__
        // WASM QBackingStore needs a deferred repaint after dock visibility changes
        QTimer::singleShot(0, this, [this]() {
            const auto children = findChildren<QWidget*>();
            for (QWidget* child : children) {
                if (child->isVisible()) child->update();
            }
        });
#endif
    });

    // Set Default Active
    activityBar_->setActive("explorer");
    currentSidebarId = "explorer";
    if (projectDock) projectDock->show();
}

void MainWindow::setupCommandPalette() {
    using namespace acutesim::gui::widgets;

    commandPalette_ = new CommandPalette(this);

    // Register common commands
    CommandPalette::Command cmd;

    // File commands
    cmd.id = "file.new";
    cmd.label = "New Project";
    cmd.category = "File";
    cmd.shortcut = "Ctrl+N";
    cmd.callback = [this]() { newProject(); };
    commandPalette_->registerCommand(cmd);

    cmd.id = "file.open";
    cmd.label = "Open Project";
    cmd.category = "File";
    cmd.shortcut = "Ctrl+O";
    cmd.callback = [this]() { openProject(); };
    commandPalette_->registerCommand(cmd);

    // View commands
    cmd.id = "view.explorer";
    cmd.label = "Show Explorer";
    cmd.category = "View";
    cmd.shortcut = "Ctrl+Shift+E";
    cmd.callback = [this]() {
        if (activityBar_) activityBar_->setActive("explorer");
        if (projectDock) projectDock->show();
    };
    commandPalette_->registerCommand(cmd);

    cmd.id = "view.components";
    cmd.label = "Show Components";
    cmd.category = "View";
    cmd.shortcut = "Ctrl+Shift+C";
    cmd.callback = [this]() {
        if (activityBar_) activityBar_->setActive("components");
        if (libraryDock) libraryDock->show();
    };
    commandPalette_->registerCommand(cmd);

    cmd.id = "view.copilot";
    cmd.label = "Show AI Copilot";
    cmd.category = "View";
    cmd.shortcut = "Ctrl+Shift+A";
    cmd.callback = [this]() {
        if (aiDock) {
            aiDock->show();
            aiDock->raise();
        }
    };
    commandPalette_->registerCommand(cmd);

    cmd.id = "view.problems";
    cmd.label = "Show Problems Panel";
    cmd.category = "View";
    cmd.shortcut = "Ctrl+Shift+M";
    cmd.callback = [this]() {
        if (messagesDock) {
            messagesDock->show();
            messagesDock->raise();
        }
    };
    commandPalette_->registerCommand(cmd);

    cmd.id = "view.output";
    cmd.label = "Show Output Panel";
    cmd.category = "View";
    cmd.shortcut = "Ctrl+Shift+U";
    cmd.callback = [this]() {
        if (outputDock_) {
            outputDock_->show();
            outputDock_->raise();
        }
    };
    commandPalette_->registerCommand(cmd);

    // Phase 2: DC Annotations toggle
    cmd.id = "view.dcAnnotations";
    cmd.label = "Toggle DC Annotations";
    cmd.category = "View";
    cmd.shortcut = "Ctrl+Shift+D";
    cmd.callback = [this]() { if (viewDCAnnotationsAction) viewDCAnnotationsAction->toggle(); };
    commandPalette_->registerCommand(cmd);

    // Simulation commands
    cmd.id = "simulation.run";
    cmd.label = "Run Simulation";
    cmd.category = "Simulation";
    cmd.shortcut = "F5";
    cmd.callback = [this]() {
        if (simulationWorkspace && schematicEditor) {
            simulationWorkspace->setSchematicEditor(schematicEditor);
            simulationWorkspace->runCurrent();
            if (simulationDock_ && !simulationDock_->isVisible()) {
                simulationDock_->show();
            }
        } else {
            statusBar()->showMessage("No schematic loaded for simulation", 3000);
        }
    };
    commandPalette_->registerCommand(cmd);

    cmd.id = "simulation.runQuickDC";
    cmd.label = "Run Quick DC Analysis";
    cmd.category = "Simulation";
    cmd.shortcut = "";
    cmd.callback = [this]() {
        if (!simulationWorkspace) {
            statusBar()->showMessage("Simulation workspace not available", 3000);
            return;
        }
        simulationWorkspace->runQuickDC();
    };
    commandPalette_->registerCommand(cmd);

    cmd.id = "simulation.stop";
    cmd.label = "Stop Simulation";
    cmd.category = "Simulation";
    cmd.shortcut = "Shift+F5";
    cmd.callback = [this]() {
        if (!simulationWorkspace) {
            statusBar()->showMessage("Simulation workspace not available", 3000);
            return;
        }
        simulationWorkspace->stopSimulation();
    };
    commandPalette_->registerCommand(cmd);

    cmd.id = "simulation.configureCorners";
    cmd.label = "Configure PVT Corners";
    cmd.category = "Simulation";
    cmd.shortcut = "";
    cmd.callback = [this]() {
        if (!simulationWorkspace) {
            statusBar()->showMessage("Simulation workspace not available", 3000);
            return;
        }
        simulationWorkspace->configureCorners();
    };
    commandPalette_->registerCommand(cmd);

    // Schematic commands
    cmd.id = "schematic.runERC";
    cmd.label = "Run ERC Check";
    cmd.category = "Schematic";
    cmd.shortcut = "";
    cmd.callback = [this]() {
        if (!schematicEditor) {
            statusBar()->showMessage("No schematic open - please create or open a schematic first", 3000);
            return;
        }
        schematicEditor->runERC();
    };
    commandPalette_->registerCommand(cmd);

    // Sync commands
    // Sync commands removed - functionality will be reimplemented in EditorManager/SimAutoDetector

    // Layout commands
    cmd.id = "layout.reset";
    cmd.label = "Reset Window Layout";
    cmd.category = "Layout";
    cmd.shortcut = "";
    cmd.callback = [this]() {
        acutesim::gui::core::LayoutManager::instance().resetToFactory();
        statusBar()->showMessage("Window layout reset to default", 3000);
    };
    commandPalette_->registerCommand(cmd);

    cmd.id = "layout.save";
    cmd.label = "Save Layout As...";
    cmd.category = "Layout";
    cmd.shortcut = "";
    cmd.callback = [this]() {
        using namespace acutesim::gui::widgets;
        InlineDialog::getText(this, "Save Layout", "Enter layout name:", "My Layout",
            [this](const DialogResult& r) {
                if (r.clickedButton == DialogButton::Cancel || r.textValue.isEmpty()) return;
                acutesim::gui::core::LayoutManager::instance().saveLayoutAs(r.textValue, this);
                statusBar()->showMessage("Layout saved: " + r.textValue, 3000);
            });
    };
    commandPalette_->registerCommand(cmd);

    // Built-in layout presets
    cmd.id = "layout.default";
    cmd.label = "Layout: Default";
    cmd.category = "Layout";
    cmd.shortcut = "";
    cmd.callback = [this]() {
        acutesim::gui::core::LayoutManager::instance().loadLayout("default", this);
        statusBar()->showMessage("Switched to Default layout", 3000);
    };
    commandPalette_->registerCommand(cmd);

    cmd.id = "layout.analog_debug";
    cmd.label = "Layout: Analog Debugging";
    cmd.category = "Layout";
    cmd.shortcut = "";
    cmd.callback = [this]() {
        acutesim::gui::core::LayoutManager::instance().loadLayout("analog_debug", this);
        // Optimize for analog: show waveform, hide project explorer
        if (waveformDock_) { waveformDock_->show(); waveformDock_->raise(); }
        if (projectDock) projectDock->hide();
        if (activityBar_) activityBar_->setActive("components");
        statusBar()->showMessage("Switched to Analog Debugging layout", 3000);
    };
    commandPalette_->registerCommand(cmd);

    cmd.id = "layout.rtl_synthesis";
    cmd.label = "Layout: RTL Synthesis";
    cmd.category = "Layout";
    cmd.shortcut = "";
    cmd.callback = [this]() {
        acutesim::gui::core::LayoutManager::instance().loadLayout("rtl_synthesis", this);
        // Show HDL editor, hide waveform
        if (hdlToggleAction) hdlToggleAction->setChecked(true);
        if (waveformDock_) waveformDock_->hide();
        statusBar()->showMessage("Switched to RTL Synthesis layout", 3000);
    };
    commandPalette_->registerCommand(cmd);

    cmd.id = "layout.simulation";
    cmd.label = "Layout: Simulation Focus";
    cmd.category = "Layout";
    cmd.shortcut = "";
    cmd.callback = [this]() {
        acutesim::gui::core::LayoutManager::instance().loadLayout("simulation", this);
        // Show all bottom panels
        if (waveformDock_) { waveformDock_->show(); waveformDock_->raise(); }
        if (outputDock_) outputDock_->show();
        if (messagesDock) messagesDock->show();
        statusBar()->showMessage("Switched to Simulation Focus layout", 3000);
    };
    commandPalette_->registerCommand(cmd);

    cmd.id = "layout.minimal";
    cmd.label = "Layout: Minimal";
    cmd.category = "Layout";
    cmd.shortcut = "";
    cmd.callback = [this]() {
        acutesim::gui::core::LayoutManager::instance().loadLayout("minimal", this);
        // Hide all side panels for maximum canvas
        if (projectDock) projectDock->hide();
        if (libraryDock) libraryDock->hide();
        if (aiDock) aiDock->hide();
        if (waveformDock_) waveformDock_->hide();
        if (outputDock_) outputDock_->hide();
        if (messagesDock) messagesDock->hide();
        if (activityBar_) activityBar_->setActive("");
        statusBar()->showMessage("Switched to Minimal layout", 3000);
    };
    commandPalette_->registerCommand(cmd);

    // Delete user layout
    cmd.id = "layout.delete";
    cmd.label = "Delete Layout...";
    cmd.category = "Layout";
    cmd.shortcut = "";
    cmd.callback = [this]() {
        using namespace acutesim::gui::widgets;
        auto layouts = acutesim::gui::core::LayoutManager::instance().availableLayouts();
        QStringList userLayouts;
        QStringList userLayoutIds;
        for (const auto& l : layouts) {
            if (!l.isBuiltIn) {
                userLayouts << l.name;
                userLayoutIds << l.id;
            }
        }
        if (userLayouts.isEmpty()) {
            statusBar()->showMessage("No user layouts to delete", 3000);
            return;
        }
        InlineDialog::getItem(this, "Delete Layout",
            "Select layout to delete:", userLayouts, 0,
            [this, userLayouts, userLayoutIds](const DialogResult& r) {
                if (r.clickedButton == DialogButton::Cancel) return;
                QString name = r.textValue;
                if (!name.isEmpty()) {
                    int idx = userLayouts.indexOf(name);
                    if (idx >= 0 && idx < userLayoutIds.size()) {
                        QString id = userLayoutIds.at(idx);
                        if (acutesim::gui::core::LayoutManager::instance().deleteLayout(id)) {
                            statusBar()->showMessage("Layout deleted: " + name, 3000);
                        } else {
                            statusBar()->showMessage("Failed to delete layout: " + name, 3000);
                        }
                    }
                }
            });
    };
    commandPalette_->registerCommand(cmd);

    // Register keybindings using the centralized KeybindingManager
    setupKeybindings();
}

void MainWindow::setupKeybindings() {
    using namespace acutesim::gui::core;
    auto& km = KeybindingManager::instance();

    // =========================================================================
    // General / Global
    // =========================================================================
    km.registerCallback("general.commandPalette", "Command Palette", "General",
                       QKeySequence("Ctrl+Shift+P"),
                       [this]() { showCommandPalette(); }, this, "global");

    km.registerCallback("general.help", "Help / Command Palette", "General",
                       QKeySequence("F1"),
                       [this]() { showCommandPalette(); }, this, "global");

    km.registerCallback("general.keybindings", "Keyboard Shortcuts", "General",
                       QKeySequence("Ctrl+K Ctrl+S"),
                       [this]() { showKeybindingsDialog(); }, this, "global");

    // =========================================================================
    // File Operations
    // =========================================================================
    km.registerCallback("file.newProject", "New Project", "File",
                       QKeySequence("Ctrl+N"),
                       [this]() { newProject(); }, this, "global");

    km.registerCallback("file.openProject", "Open Project", "File",
                       QKeySequence("Ctrl+O"),
                       [this]() { openProject(); }, this, "global");

    km.registerCallback("file.save", "Save", "File",
                       QKeySequence("Ctrl+S"),
                       [this]() { statusBar()->showMessage("Saved", 2000); }, this, "global");

    // =========================================================================
    // Simulation
    // =========================================================================
    km.registerCallback("simulation.run", "Run Simulation", "Simulation",
                       QKeySequence("F5"),
                       [this]() {
                           if (simulationWorkspace && schematicEditor) {
                               simulationWorkspace->setSchematicEditor(schematicEditor);
                               simulationWorkspace->runCurrent();
                               if (simulationDock_ && !simulationDock_->isVisible()) {
                                   simulationDock_->show();
                               }
                           } else {
                               statusBar()->showMessage("No schematic loaded for simulation", 3000);
                           }
                       }, this, "global");

    km.registerCallback("simulation.stop", "Stop Simulation", "Simulation",
                       QKeySequence("Shift+F5"),
                       [this]() {
                           if (simulationWorkspace) {
                               simulationWorkspace->stopSimulation();
                           }
                       }, this, "global");

    km.registerCallback("simulation.runERC", "Run ERC Check", "Simulation",
                       QKeySequence("Ctrl+Shift+E"),
                       [this]() {
                           if (schematicEditor) {
                               QStringList violations = schematicEditor->runERC();
                               if (violations.isEmpty()) {
                                   statusBar()->showMessage("ERC passed - no violations", 2000);
                               } else {
                                   statusBar()->showMessage(QString("ERC found %1 violations").arg(violations.size()), 2000);
                               }
                           }
                       }, this, "global");

    // =========================================================================
    // View / Layout
    // =========================================================================
    km.registerCallback("view.toggleSidebar", "Toggle Sidebar", "View",
                       QKeySequence("Ctrl+B"),
                       [this]() {
                           if (projectDock && projectDock->isVisible()) {
                               projectDock->hide();
                               if (libraryDock) libraryDock->hide();
                           } else {
                               if (projectDock) projectDock->show();
                           }
                       }, this, "global");

    km.registerCallback("view.toggleBottomPanel", "Toggle Bottom Panel", "View",
                       QKeySequence("Ctrl+J"),
                       [this]() {
                           bool anyVisible = (outputDock_ && outputDock_->isVisible()) ||
                                           (messagesDock && messagesDock->isVisible());
                           if (anyVisible) {
                               if (outputDock_) outputDock_->hide();
                               if (messagesDock) messagesDock->hide();
                           } else {
                               if (outputDock_) outputDock_->show();
                           }
                       }, this, "global");

    km.registerCallback("view.zoomIn", "Zoom In", "View",
                       QKeySequence("Ctrl+="),
                       [this]() {
                           if (schematicEditor && schematicEditor->getView()) {
                               schematicEditor->getView()->scale(1.2, 1.2);
                           }
                       }, this, "editor");

    km.registerCallback("view.zoomOut", "Zoom Out", "View",
                       QKeySequence("Ctrl+-"),
                       [this]() {
                           if (schematicEditor && schematicEditor->getView()) {
                               schematicEditor->getView()->scale(1.0/1.2, 1.0/1.2);
                           }
                       }, this, "editor");

    km.registerCallback("view.fitToWindow", "Fit to Window", "View",
                       QKeySequence("Ctrl+0"),
                       [this]() {
                           if (schematicEditor && schematicEditor->getView() && schematicEditor->getScene()) {
                               schematicEditor->getView()->fitInView(schematicEditor->getScene()->sceneRect(), Qt::KeepAspectRatio);
                           }
                       }, this, "editor");

    km.registerAction("view.dcAnnotations", "Toggle DC Annotations", "View",
                      QKeySequence("Ctrl+Shift+D"), viewDCAnnotationsAction, "global");

    // =========================================================================
    // Editor
    // =========================================================================
    km.registerCallback("editor.delete", "Delete Selected", "Editor",
                       QKeySequence("Delete"),
                       [this]() {
                           if (schematicEditor && schematicEditor->getScene()) {
                               // Delete selected items from scene
                               QList<QGraphicsItem*> selected = schematicEditor->getScene()->selectedItems();
                               for (auto* item : selected) {
                                   schematicEditor->getScene()->removeItem(item);
                                   delete item;
                               }
                           }
                       }, this, "editor");

    km.registerCallback("editor.selectAll", "Select All", "Editor",
                       QKeySequence("Ctrl+A"),
                       [this]() {
                           if (schematicEditor && schematicEditor->getScene()) {
                               for (auto* item : schematicEditor->getScene()->items()) {
                                   item->setSelected(true);
                               }
                           }
                       }, this, "editor");

    km.registerCallback("editor.escape", "Cancel / Deselect", "Editor",
                       QKeySequence("Escape"),
                       [this]() {
                           if (schematicEditor && schematicEditor->getScene()) {
                               schematicEditor->getScene()->clearSelection();
                           }
                       }, this, "editor");

    qDebug() << "[MW] Keybindings registered:" << km.getAllBindings().size();
}

void MainWindow::showCommandPalette() {
    if (commandPalette_) {
        commandPalette_->show();
    }
}

void MainWindow::showKeybindingsDialog() {
    using namespace acutesim::gui::widgets;
    auto* widget = new KeybindingsDialog();
    InlineDialog::showCustom(this, "Keyboard Shortcuts", widget,
        DialogButton::Ok, nullptr);
}

void MainWindow::applyModernTabStyle(QTabWidget* tabWidget) {
    if (!tabWidget) return;

    // Get theme colors
    using namespace acutesim::gui::styling;
    auto& tm = ThemeManager::instance();

    QString tabBg = tm.getColor("tab.background").name();
    QString tabActiveBg = tm.getColor("tab.activeBackground").name();
    QString tabHoverBg = tm.getColor("tab.hoverBackground").name();
    QString tabBorder = tm.getColor("tab.border").name();
    QString tabAccent = tm.getColor("tab.activeBorderTop").name();
    QString textColor = tm.getColor("ui.text").name();
    QString textDim = tm.getColor("ui.textDim").name();

    QString closeButtonImageRule = "image: url(:/icons/close.svg);";

    // Modern VS Code-style tab styling with rounded corners
    tabWidget->setStyleSheet(QString(R"(
        QTabWidget::pane {
            background-color: %2;
            border: none;
            border-top: 1px solid %4;
        }
        QTabWidget::tab-bar {
            left: 0px;
        }
        QTabBar {
            background-color: %1;
            border: none;
        }
        QTabBar::tab {
            background-color: %1;
            color: %6;
            border: none;
            border-top-left-radius: 4px;
            border-top-right-radius: 4px;
            padding: 8px 16px;
            margin-right: 2px;
            min-width: 80px;
        }
        QTabBar::tab:hover {
            background-color: %3;
            color: %5;
        }
        QTabBar::tab:selected {
            background-color: %2;
            color: %5;
            border-top: 2px solid %7;
        }
        QTabBar::close-button {
            %8
            width: 14px;
            height: 14px;
            subcontrol-position: right;
            padding: 2px;
        }
        QTabBar::close-button:hover {
            background-color: rgba(255, 255, 255, 0.1);
            border-radius: 3px;
        }
    )").arg(tabBg, tabActiveBg, tabHoverBg, tabBorder, textColor, textDim, tabAccent,
            closeButtonImageRule));
}

void MainWindow::updateLayoutMenu() {
    if (!layoutMenu_) return;

    layoutMenu_->clear();

    auto& lm = acutesim::gui::core::LayoutManager::instance();
    auto layouts = lm.availableLayouts();
    QString currentId = lm.currentLayout();

    // Built-in presets
    bool hasBuiltIn = false;
    for (const auto& preset : layouts) {
        if (preset.isBuiltIn) {
            hasBuiltIn = true;
            QAction* action = layoutMenu_->addAction(preset.name);
            action->setData(preset.id);
            action->setCheckable(true);
            action->setChecked(preset.id == currentId);
            action->setToolTip(preset.description);
            connect(action, &QAction::triggered, this, [this, id = preset.id]() {
                acutesim::gui::core::LayoutManager::instance().loadLayout(id, this);
            });
        }
    }

    // User layouts section
    bool hasUserLayouts = false;
    for (const auto& preset : layouts) {
        if (!preset.isBuiltIn) {
            if (!hasUserLayouts) {
                layoutMenu_->addSeparator();
                hasUserLayouts = true;
            }
            QAction* action = layoutMenu_->addAction(preset.name);
            action->setData(preset.id);
            action->setCheckable(true);
            action->setChecked(preset.id == currentId);
            action->setToolTip(preset.description);
            connect(action, &QAction::triggered, this, [this, id = preset.id]() {
                acutesim::gui::core::LayoutManager::instance().loadLayout(id, this);
            });
        }
    }

    // Management actions
    layoutMenu_->addSeparator();

    layoutMenu_->addAction("Save Layout As...", this, [this]() {
        using namespace acutesim::gui::widgets;
        InlineDialog::getText(this, "Save Layout", "Enter layout name:", "My Layout",
            [this](const DialogResult& r) {
                if (r.clickedButton == DialogButton::Cancel || r.textValue.isEmpty()) return;
                acutesim::gui::core::LayoutManager::instance().saveLayoutAs(r.textValue, this);
                statusBar()->showMessage("Layout saved: " + r.textValue, 3000);
            });
    });

    layoutMenu_->addAction("Reset to Default", this, &MainWindow::resetLayout);
}

void MainWindow::resetLayout() {
    acutesim::gui::core::LayoutManager::instance().resetToFactory();
    statusBar()->showMessage("Window layout reset to default", 3000);
}
