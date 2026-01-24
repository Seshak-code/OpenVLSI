#include "mainwindow.h"
#include "welcome_widget.h"
#include "layout_manager.h"
#include "docking_manager.h"
#include "activity_bar.h"
#include <QApplication>
#include <QHeaderView>
#include <QStatusBar>
#include <QDebug>
#include <QComboBox>
#include <QMessageBox>
#include <QPushButton>
#include <QLabel>
#include <QInputDialog>
#include <QToolButton>
#include <QMenu>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    qDebug() << "[MW INIT] Constructor start";
    setWindowTitle("AcuteSim - ASIC Design Suite");
    resize(1280, 720);
    
    // NASA Principle: Enable dock nesting for flexible workspace layout
    // Allows up to 3-way horizontal splits of bottom panel docks (Results, Waveforms, etc.)
    setDockNestingEnabled(true);
    
    qDebug() << "[MW INIT] Creating DesignFlowManager...";
    // Initialize Design Flow Manager
    flowManager = new DesignFlowManager(this);
    connect(flowManager, &DesignFlowManager::stageChanged, this, &MainWindow::onStageChanged);

    // Initialize Docking Manager (Phase 11.2)
    dockingManager = new DockingManager(this);
    
    qDebug() << "[MW INIT] setupCentralWidget...";
    // 1. Setup Central Area (Stage-based Workspace)
    setupCentralWidget();

    qDebug() << "[MW INIT] setupDocks...";
    // 2. Setup Docks (Project, Properties, Messages)
    setupDocks();

    qDebug() << "[MW INIT] setupActivityBar...";
    setupActivityBar();

    qDebug() << "[MW INIT] setupMenus...";
    // 3. Setup Menus and Toolbar
    setupMenus();
    
    qDebug() << "[MW INIT] setupStageToolbar...";
    setupStageToolbar();

    // 4. Status Bar
    statusBar()->showMessage("Ready - RTL Design Stage");
    
    // 5. Restore Layout (Phase 11.2 State Persistence)
    // We delay this slightly or call after UI init
    acutesim::gui::core::LayoutManager::instance().restoreWindowLayout("main_window", this);
    
    qDebug() << "[MW INIT] Constructor complete";
}

void MainWindow::setupCentralWidget() {
    // ==== Stage Tabs (Top-Level) ====
    stageTabs = new QTabWidget(this);
    stageTabs->setTabPosition(QTabWidget::North);
    stageTabs->setDocumentMode(true); // VS Code style flat tabs
    
    // --- Stage 1: RTL Design (Multi-Page Schematics) ---
    QWidget* rtlContainer = new QWidget();
    QVBoxLayout* rtlLayout = new QVBoxLayout(rtlContainer);
    rtlLayout->setContentsMargins(0, 0, 0, 0);
    
    // Splitter for Hybrid View (Schematic | Code)
    QSplitter* rtlSplitter = new QSplitter(Qt::Horizontal);
    
    // Left: Schematic Tabs
    rtlPageTabs = new QTabWidget();
    rtlPageTabs->setTabsClosable(true);
    rtlPageTabs->setMovable(true);
    
    // Welcome Widget (shown when no schematics are open)
    welcomeWidget = new WelcomeWidget(this);
    rtlPageTabs->addTab(welcomeWidget, "Welcome");
    connect(welcomeWidget, &WelcomeWidget::newSchematicRequested, this, &MainWindow::createSchematicFromWelcome);
    
    // Track active editor when tabs change
    connect(rtlPageTabs, &QTabWidget::currentChanged, this, &MainWindow::onRtlTabChanged);
    
    // Initialize schematicEditor as nullptr until first schematic is created
    schematicEditor = nullptr;
    
    // "+" button to add new pages
    QPushButton* addPageBtn = new QPushButton("+");
    addPageBtn->setFixedWidth(30);
    addPageBtn->setToolTip("Add new schematic page");
    connect(addPageBtn, &QPushButton::clicked, this, &MainWindow::addRtlPage);
    rtlPageTabs->setCornerWidget(addPageBtn, Qt::TopRightCorner);
    
    rtlSplitter->addWidget(rtlPageTabs);
    
    // Right: HDL Code Editor (Phase 14.1)
    hdlEditor = new HDLEditor(this); // Assign to member
    hdlEditor->setLanguage(HDLSyntaxHighlighter::VERILOG_AMS); // Default to Mixed-Signal
    rtlSplitter->addWidget(hdlEditor);
    
    // Store splitter reference for HDL toggle
    rtlSplitter_ = rtlSplitter;
    
    // Set Initial Ratios (70% Schematic, 30% Code)
    rtlSplitter->setStretchFactor(0, 7);
    rtlSplitter->setStretchFactor(1, 3);
    
    rtlLayout->addWidget(rtlSplitter);
    stageTabs->addTab(rtlContainer, "RTL Design");
    
    // --- Stage 2: Synthesis (Placeholder) ---
    QLabel* synthesisLabel = new QLabel("Logic Synthesis & Timing Optimization\n\n[Run Synthesis] button coming soon...");
    synthesisLabel->setAlignment(Qt::AlignCenter);
    stageTabs->addTab(synthesisLabel, "Synthesis");
    
    // --- Stage 3: Physical Design (Placeholder) ---
    QLabel* physicalLabel = new QLabel("Physical Design: Netlist → Layout\n\nFloorplanning, P&R, Power Grid visualization.");
    physicalLabel->setAlignment(Qt::AlignCenter);
    stageTabs->addTab(physicalLabel, "Physical Design");
    
    // --- Stage 4: Verification (Placeholder) ---
    QLabel* verificationLabel = new QLabel("DRC/LVS Checks & Sign-Off\n\n(Future: 3D LVS Viewer)");
    verificationLabel->setAlignment(Qt::AlignCenter);
    stageTabs->addTab(verificationLabel, "Verification");
    
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
        else if (idx == 1) flowManager->setStage(DesignStage::SYNTHESIS);
        else if (idx == 2) flowManager->setStage(DesignStage::PHYSICAL_DESIGN);
        else if (idx == 3) flowManager->setStage(DesignStage::VERIFICATION);
    });
    
    // Connect RTL page tab changes to update active schematic
    connect(rtlPageTabs, &QTabWidget::currentChanged, this, [this](int idx) {
        if (idx >= 0) {
            SchematicEditor* newEditor = qobject_cast<SchematicEditor*>(rtlPageTabs->widget(idx));
            
            // If switching to a new editor, disconnect old and connect new
            if (newEditor && newEditor != schematicEditor) {
                // Disconnect from old editor if it exists
                if (schematicEditor) {
                    disconnect(schematicEditor, &SchematicEditor::itemSelected, this, &MainWindow::updateProperties);
                    disconnect(schematicEditor, &SchematicEditor::wireSelected, this, &MainWindow::updateWireProperties);
                }
                
                schematicEditor = newEditor;
                
                // Connect to new editor
                connect(schematicEditor, &SchematicEditor::itemSelected, this, &MainWindow::updateProperties);
                connect(schematicEditor, &SchematicEditor::wireSelected, this, &MainWindow::updateWireProperties);
                
                // Update simulation workspace if it exists
                if (simulationWorkspace) {
                    simulationWorkspace->setSchematicEditor(newEditor);
                }
                
                // Update Debug Suggestions Context
                if (debugSuggestionsPanel) {
                    debugSuggestionsPanel->setEditor(newEditor);
                    debugSuggestionsPanel->refreshSuggestions();
                }

                // [Fix] Update AI Context
                if (aiPanel) {
                    aiPanel->setEditor(newEditor);
                }
                
            }
            
            // Update UI to match current page's mode
            if (schematicEditor && libraryPanel) {
                libraryPanel->setMode(schematicEditor->getMode());
            }
        }
    });
    
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
    
    // NOTE: Do NOT tabify. We want exclusive single dock.
    
    // Initial Sync: Component Library starts in the same mode as the initial schematic
    if (libraryPanel && schematicEditor) {
        libraryPanel->setMode(schematicEditor->getMode());
    }

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
    
    // Wire signals
    connect(dockingManager, &DockingManager::tabPromotionRequested, this, &MainWindow::addCentralTab);
    connect(dockingManager, &DockingManager::dockRestorationRequested, this, &MainWindow::removeCentralTab);
    
    TerminalWidget* term = dockingManager->terminal();
    if (term) {
        connect(term, &TerminalWidget::commandRequested, this, [this](const QString& cmd){
            if (cmd.startsWith("sync -to-hdl")) syncSchematicToVerilog();
            else if (cmd.startsWith("sync -from-hdl")) syncVerilogToSchematic();
            else if (cmd == "layout -reset") acutesim::gui::core::LayoutManager::instance().resetToFactory();
        });
    }

    qDebug() << "[MW] Retrieving Docks from DockingManager...";
    messagesDock = dockingManager->getDock("analysis"); // Contains Problems + Debug panels
    outputDock_ = dockingManager->getDock("logs");      // Contains Results + Output
    waveformDock_ = dockingManager->getDock("waveforms");

    // Get widgets from DockingManager (no longer creating legacy docks)
    problemsPanel = dockingManager->problems();
    resultsTree_ = dockingManager->results();

    // Note: streamingDock_ and resultsDock_ are now obsolete
    // DockingManager handles all bottom panel docks
    streamingDock_ = nullptr;
    resultsDock_ = nullptr;
    
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
    projectDock = new QDockWidget("Project Browser", this);
    projectDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    // VS Code style: Hide native title bar
    projectDock->setTitleBarWidget(new QWidget());
    projectDock->setFeatures(QDockWidget::NoDockWidgetFeatures); // Disable floating/closing

    // File Model
    fileModel = new QFileSystemModel(this);
    fileModel->setRootPath(QDir::currentPath());
    fileModel->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
    
    // View
    projectView = new QTreeView(projectDock);
    projectView->setModel(fileModel);
    projectView->setRootIndex(fileModel->index(QDir::currentPath()));
    projectView->setSortingEnabled(true);
    projectView->setColumnHidden(1, true); // Hide Size
    projectView->setColumnHidden(2, true); // Hide Type
    projectView->setColumnHidden(3, true); // Hide Date
    projectView->setAlternatingRowColors(true);
    
    // Style
    projectView->setStyleSheet("QTreeView { font-size: 11pt; }");
    
    // Connect double click to open (Action placeholder)
    connect(projectView, &QTreeView::doubleClicked, this, [this](const QModelIndex& index){
        QString path = fileModel->filePath(index);
        statusBar()->showMessage("Selected file: " + path);
    });
    
    projectDock->setWidget(projectView);
}

void MainWindow::createLibraryDock() {
    libraryDock = new QDockWidget("Component Library", this);
    libraryDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    // VS Code style: Hide native title bar
    libraryDock->setTitleBarWidget(new QWidget());
    libraryDock->setFeatures(QDockWidget::NoDockWidgetFeatures); // Disable floating/closing

    libraryPanel = new ComponentLibrary(this);
    
    // Connect Selection
    connect(libraryPanel, &ComponentLibrary::componentSelected, this, [this](ComponentType type){
        if (schematicEditor) {
            schematicEditor->setTool(type);
        }
    });
    
    libraryDock->setWidget(libraryPanel);
}

void MainWindow::createPropertiesDock() {
    propertiesDock = new QDockWidget("Properties", this);
    propertiesDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    
    // Stack for Multi-Mode Support
    propertiesStack = new QStackedWidget(propertiesDock);
    
    // Mode 1: Component Properties (Table)
    propertiesTable = new QTableWidget();
    propertiesTable->setColumnCount(2);
    propertiesTable->setHorizontalHeaderLabels({"Property", "Value"});
    propertiesTable->horizontalHeader()->setStretchLastSection(true);
    propertiesTable->verticalHeader()->setVisible(false);
    propertiesTable->setAlternatingRowColors(true);
    
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
    int paramCount = item->params.size();
    propertiesTable->setRowCount(3 + paramCount);
    
    // 0: Name
    propertiesTable->setItem(0, 0, new QTableWidgetItem("Name"));
    propertiesTable->setItem(0, 1, new QTableWidgetItem(item->name));
    
    // 1: Value (Context Aware Label)
    QString valueLabel = "Value";
    switch(item->type) {
        case ComponentType::Resistor: valueLabel = "Resistance"; break;
        case ComponentType::Capacitor: valueLabel = "Capacitance"; break;
        case ComponentType::Inductor: valueLabel = "Inductance"; break;
        case ComponentType::VoltageSource: valueLabel = "DC Voltage"; break;
        default: break;
    }
    
    propertiesTable->setItem(1, 0, new QTableWidgetItem(valueLabel));
    propertiesTable->setItem(1, 1, new QTableWidgetItem(item->value));
    
    // 2: Type (Read only)
    propertiesTable->setItem(2, 0, new QTableWidgetItem("Type"));
    QTableWidgetItem* typeItem = new QTableWidgetItem(QString::number((int)item->type)); 
    typeItem->setFlags(typeItem->flags() & ~Qt::ItemIsEditable);
    propertiesTable->setItem(2, 1, typeItem);
    
    // 3+: Dynamic Params
    int row = 3;
    QMapIterator<QString, QString> i(item->params);
    while (i.hasNext()) {
        i.next();
        QString key = i.key();
        QString val = i.value();
        
        QTableWidgetItem* keyItem = new QTableWidgetItem(key);
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
                   // Trigger re-draw or logic update if needed?
                }
            });
            
            propertiesTable->setCellWidget(row, 1, combo);
        } else {
            // Standard Text Edit
            propertiesTable->setItem(row, 1, new QTableWidgetItem(val));
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
        if (col == 1) { // Value column
            if (row == 0) {
                activeComponent->name = item->text();
            } else if (row == 1) {
                activeComponent->value = item->text();
            } else if (row > 2) {
                // Dynamic Param
                QString key = propertiesTable->item(row, 0)->text();
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
    // NASA: Allow all areas for flexible splitting within bottom panel
    messagesDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    
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

void MainWindow::createOutputDock() {
    outputDock_ = new QDockWidget("Output", this);
    // NASA: Allow all areas for flexible splitting within bottom panel
    outputDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
    
    outputLog_ = new QPlainTextEdit(outputDock_);
    outputLog_->setReadOnly(true);
    outputLog_->setStyleSheet("background: #1e1e1e; color: #ccc; font-family: monospace;");
    outputDock_->setWidget(outputLog_);
}

void MainWindow::createStreamingDock() {
    streamingDock_ = new QDockWidget("Streaming", this);
    // NASA: Allow all areas for flexible splitting within bottom panel
    streamingDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
    
    streamingLog_ = new QPlainTextEdit(streamingDock_);
    streamingLog_->setReadOnly(true);
    streamingLog_->setStyleSheet("background: #1e1e1e; color: #4ec9b0; font-family: monospace;");
    streamingDock_->setWidget(streamingLog_);
}

void MainWindow::createResultsDock() {
    resultsDock_ = new QDockWidget("Results", this);
    // NASA: Allow all areas for flexible splitting within bottom panel
    resultsDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
    
    resultsTree_ = new QTreeWidget(resultsDock_);
    resultsTree_->setHeaderLabels({"Node", "Value"});
    resultsTree_->setColumnWidth(0, 150);
    resultsTree_->setStyleSheet("QTreeWidget { background: #1e1e1e; color: #ccc; border: none; } "
                               "QHeaderView::section { background: #2d2d2d; color: #888; border: none; padding: 4px; }");
    resultsDock_->setWidget(resultsTree_);
}

void MainWindow::createWaveformDock() {
    waveformDock_ = new QDockWidget("Waveforms", this);
    // NASA: Allow all areas for flexible splitting within bottom panel
    waveformDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
    
    waveformLog_ = new QPlainTextEdit(waveformDock_);
    waveformLog_->setReadOnly(true);
    waveformLog_->setStyleSheet("background: #1e1e1e; color: #4ec9b0; font-family: monospace;");
    waveformLog_->setPlaceholderText("Waveform data points will define trace here...");
    waveformDock_->setWidget(waveformLog_);
}

void MainWindow::onSimulationResults(const QJsonObject& results) {
    QString type = results["type"].toString();
    
    // --- 1. Update Waveform Widget (Phase 11.3 - Multi-trace support) ---
    if (dockingManager) {
        if (WaveformWidget* wf = dockingManager->waveform()) {
            if (type == "TransientPoint") {
                double t = results["time"].toDouble();
                QJsonArray volts = results["voltages"].toArray();
                if (!volts.isEmpty()) {
                    // Pass all voltages as separate traces (one per node)
                    QVector<double> values;
                    for (int i = 0; i < volts.size(); ++i) {
                        values.append(volts[i].toDouble());
                    }
                    wf->appendDataPoints(t, values);
                }
            } else if (type == "DC") {
                if (results.contains("corner") && results["corner"].toString() == "Nominal") {
                    wf->clear();
                }
            }
        }
    }

    // --- 2. Update Legacy Logs (Phase 7) ---
    if (type == "DC") {
        if (resultsTree_) {
            resultsTree_->clear();
            QJsonArray voltages = results["voltages"].toArray();
            for (int i = 0; i < voltages.size(); ++i) {
                QTreeWidgetItem* item = new QTreeWidgetItem(resultsTree_);
                item->setText(0, QString("V(%1)").arg(i+1));
                item->setText(1, QString("%1 V").arg(voltages[i].toDouble(), 0, 'f', 6));
            }
        }
        // Show output dock with results
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
    fileMenu->addAction(QIcon::fromTheme("document-new"), "New Project", this, &MainWindow::newProject);
    fileMenu->addAction(QIcon::fromTheme("document-open"), "Open...", this, &MainWindow::openProject);
    fileMenu->addSeparator();
    fileMenu->addAction("Exit", this, &QWidget::close);

    // Edit
    QMenu* editMenu = mb->addMenu("&Edit");
    editMenu->addAction("Undo");
    editMenu->addAction("Redo");

    // View
    QMenu* viewMenu = mb->addMenu("&View");
    viewMenu->addAction(projectDock->toggleViewAction());
    viewMenu->addAction(propertiesDock->toggleViewAction());
    viewMenu->addAction(aiDock->toggleViewAction());
    viewMenu->addAction(messagesDock->toggleViewAction());
    
    // Bottom Panel Docks - allow restoring closed/split panels
    viewMenu->addAction(outputDock_->toggleViewAction());
    viewMenu->addAction(streamingDock_->toggleViewAction());
    viewMenu->addAction(resultsDock_->toggleViewAction());
    viewMenu->addAction(waveformDock_->toggleViewAction());
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
    
    // Phase 7: HDL Editor Toggle
    hdlToggleAction = new QAction("HDL Editor", this);
    hdlToggleAction->setCheckable(true);
    hdlToggleAction->setChecked(true); // Default ON (visible)
    viewMenu->addAction(hdlToggleAction);
    connect(hdlToggleAction, &QAction::toggled, this, [this](bool checked) {
        if (hdlEditor) {
            hdlEditor->setVisible(checked);
        }
    });

    // Simulation
    QMenu* simMenu = mb->addMenu("&Simulation");
    simMenu->addAction("Set Simulation", this, &MainWindow::runSimulation);
}

void MainWindow::newProject() {
    statusBar()->showMessage("New Project created.");
}

void MainWindow::openProject() {
    statusBar()->showMessage("Open Project dialog...");
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

void MainWindow::openSchematicTab(const QString& title, const QJsonObject& content) {
    SchematicEditor* newEditor = new SchematicEditor(this);
    
    // Auto-detect mode from content logic or default to Bench
    newEditor->setMode(AppMode::BENCH_TEST); 
    
    // Load content
    newEditor->loadState(content);
    
    // Add tab
    rtlPageTabs->addTab(newEditor, title);
    rtlPageTabs->setCurrentWidget(newEditor);
    
    // Update active editor
    schematicEditor = newEditor;
    if (libraryPanel) libraryPanel->setMode(AppMode::BENCH_TEST);
    if (aiPanel) aiPanel->setEditor(newEditor);
    
    connect(newEditor, &SchematicEditor::itemSelected, this, &MainWindow::updateProperties);
    connect(newEditor, &SchematicEditor::wireSelected, this, &MainWindow::updateWireProperties);
    
    statusBar()->showMessage(QString("Opened %1").arg(title), 4000);
}

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
    propertiesTable->setItem(0, 1, new QTableWidgetItem(wire->getName()));
    
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
    
    // Stage Selector removed - tabs serve as navigation

    // Sync Button with Dropdown Menu (Phase 14.2 - VS Code style)
    QToolButton* syncButton = new QToolButton(this);
    syncButton->setText("Sync");
    syncButton->setToolTip("Synchronize Schematic and HDL");
    syncButton->setPopupMode(QToolButton::InstantPopup);

    QMenu* syncMenu = new QMenu(syncButton);

    QAction* genVerilog = syncMenu->addAction("Schematic → HDL");
    genVerilog->setToolTip("Generate Verilog-AMS from Schematic");
    connect(genVerilog, &QAction::triggered, this, &MainWindow::syncSchematicToVerilog);

    QAction* parseVerilog = syncMenu->addAction("HDL → Schematic");
    parseVerilog->setToolTip("Update Schematic from Verilog-AMS (Ghost Placement)");
    connect(parseVerilog, &QAction::triggered, this, &MainWindow::syncVerilogToSchematic);

    syncButton->setMenu(syncMenu);
    modeToolbar->addWidget(syncButton);
}

void MainWindow::syncSchematicToVerilog() {
    if (!schematicEditor || !hdlEditor) return;
    QString code = schematicEditor->generateVerilogSnippet();
    hdlEditor->setPlainText(code);
    statusBar()->showMessage("Generated Verilog-AMS from Schematic");
}

void MainWindow::syncVerilogToSchematic() {
    if (!schematicEditor || !hdlEditor) return;
    QString code = hdlEditor->toPlainText();
    schematicEditor->parseVerilogToSchematic(code);
    statusBar()->showMessage("Parsed Verilog-AMS to Schematic (Ghost Objects)");
}

void MainWindow::onStageChanged(DesignStage stage) {
    // Update Component Library based on stage
    if (libraryPanel) {
        libraryPanel->setStage(stage);
    }
    
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

void MainWindow::addRtlPage() {
    QStringList modes = {"Bench Test (Ideal)", "Block Subsystem", "System Integration"};
    bool ok;
    QString selection = QInputDialog::getItem(this, "New Schematic", 
                                             "Select simulation mode:", 
                                             modes, 0, false, &ok);
    if (!ok) return;

    static int pageCount = 0; 
    QString pageName = QString("module_%1.sch").arg(++pageCount);
    
    AppMode mode = AppMode::BENCH_TEST;
    if (selection == modes[1]) mode = AppMode::BLOCK_SUBSYSTEM;
    else if (selection == modes[2]) mode = AppMode::SYSTEM_INTEGRATION;
    
    SchematicEditor* newEditor = new SchematicEditor(this);
    newEditor->setMode(mode);

    // Add tab with mode label
    QString tabTitle = QString("%1 [%2]").arg(pageName, modeLabel(mode));
    rtlPageTabs->addTab(newEditor, tabTitle);
    rtlPageTabs->setCurrentWidget(newEditor);
    
    // Update active editor and sync library
    schematicEditor = newEditor;
    if (libraryPanel) libraryPanel->setMode(mode);
    
    connect(newEditor, &SchematicEditor::itemSelected, this, &MainWindow::updateProperties);
    connect(newEditor, &SchematicEditor::wireSelected, this, &MainWindow::updateWireProperties);
    
    statusBar()->showMessage(QString("Added %1 schematic: %2").arg(selection, pageName), 3000);
}

void MainWindow::createSchematicFromWelcome(AppMode mode) {
    static int pageCount = 0;
    QString pageName = QString("module_%1.sch").arg(++pageCount);
    
    SchematicEditor* newEditor = new SchematicEditor(this);
    newEditor->setMode(mode);
    
    // Add tab with mode label
    QString tabTitle = QString("%1 [%2]").arg(pageName, modeLabel(mode));
    rtlPageTabs->addTab(newEditor, tabTitle);
    rtlPageTabs->setCurrentWidget(newEditor);
    
    // Update active editor and sync library
    schematicEditor = newEditor;
    if (libraryPanel) libraryPanel->setMode(mode);
    
    connect(newEditor, &SchematicEditor::itemSelected, this, &MainWindow::updateProperties);
    connect(newEditor, &SchematicEditor::wireSelected, this, &MainWindow::updateWireProperties);
    
    statusBar()->showMessage(QString("Created %1 schematic: %2").arg(modeLabel(mode), pageName), 3000);
}

void MainWindow::onRtlTabChanged(int index) {
    // Phase 4: Dynamic Debug Logging
    // Disconnect previous editor triggers
    if (schematicEditor) {
        if (problemsPanel) {
            disconnect(schematicEditor, &SchematicEditor::violationsChanged, problemsPanel, &ProblemsPanel::setViolations);
        }
        // Disconnect topology change handler
        disconnect(schematicEditor, &SchematicEditor::topologyChanged, this, nullptr);
    }

    // Clear waveform when switching tabs (different circuit = stale data)
    if (dockingManager && dockingManager->waveform()) {
        dockingManager->waveform()->clear();
    }

    QWidget* current = rtlPageTabs->widget(index);
    if (auto* editor = qobject_cast<SchematicEditor*>(current)) {
        schematicEditor = editor;
        if (libraryPanel) libraryPanel->setMode(editor->getMode());
        if (aiPanel) aiPanel->setEditor(editor);
        if (simulationWorkspace) simulationWorkspace->setSchematicEditor(editor);

        // Connect new editor to problems panel
        if (problemsPanel) {
            connect(schematicEditor, &SchematicEditor::violationsChanged, problemsPanel, &ProblemsPanel::setViolations);
            // Sync initial state
            if (editor->getScene()) {
                problemsPanel->setViolations(editor->getScene()->getViolations());
            }
        }

        // Connect topology changes to clear waveform (invalidate stale results)
        connect(schematicEditor, &SchematicEditor::topologyChanged, this, [this](size_t) {
            if (dockingManager && dockingManager->waveform()) {
                dockingManager->waveform()->clear();
            }
        });

        statusBar()->showMessage(QString("Active: %1").arg(rtlPageTabs->tabText(index)));
    } else {
        // Welcome widget or placeholder - no active editor
        schematicEditor = nullptr;
        if (aiPanel) aiPanel->setEditor(nullptr);
        if (simulationWorkspace) simulationWorkspace->setSchematicEditor(nullptr);
        if (problemsPanel) problemsPanel->clear();
    }
}

QString MainWindow::modeLabel(AppMode mode) {
    switch (mode) {
        case AppMode::BENCH_TEST: return "Bench";
        case AppMode::BLOCK_SUBSYSTEM: return "Block";
        case AppMode::SYSTEM_INTEGRATION: return "System";
        default: return "Unknown";
    }
}

AppMode MainWindow::getMode() const {
    return schematicEditor ? schematicEditor->getMode() : AppMode::SYSTEM_INTEGRATION;
}

void MainWindow::setMode(AppMode mode) {
    if (schematicEditor) {
        schematicEditor->setMode(mode);
    }
    
    // Update Component Library filter
    if (libraryPanel) {
        libraryPanel->setMode(mode);
    }
}



void MainWindow::createWorkerDock() {
    workerDock = new QDockWidget("Worker Status", this);
    workerDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::RightDockWidgetArea);
    workerPanel = new acutesim::gui::orchestration::WorkerStatusPanel(workerDock);
    workerDock->setWidget(workerPanel);
}

void MainWindow::createDebugSuggestionsDock() {
    debugSuggestionsDock = new QDockWidget("AI Debug Hints", this);
    debugSuggestionsDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::RightDockWidgetArea);
    
    debugSuggestionsPanel = new acutesim::gui::widgets::DebugSuggestionsPanel(debugSuggestionsDock);
    debugSuggestionsPanel->setEditor(schematicEditor); // Set initial editor
    
    debugSuggestionsDock->setWidget(debugSuggestionsPanel);
}

void MainWindow::createSimulationDock() {
    simulationDock_ = new QDockWidget("Simulation", this);
    simulationDock_->setAllowedAreas(Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
    simulationDock_->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    
    // simulationWorkspace is already created in setupCentralWidget
    simulationDock_->setWidget(simulationWorkspace);
    
    // Start hidden - toggled via menu
    simulationDock_->hide();
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
    activityBar_->addActivity("explorer", QIcon::fromTheme("folder"), "Project Explorer");
    
    // 2. Components (Library) - Use chip/component icon
    activityBar_->addActivity("components", QIcon::fromTheme("application-x-executable"), "Components");
    
    // 3. Search (Placeholder)
    activityBar_->addActivity("search", QIcon::fromTheme("edit-find"), "Search");
    
    // 4. Copilot (AI) - Currently on Right, maybe move to Left?
    // User requested VS Code style. Copilot is usually a Chat view in Secondary Sidebar or Panel.
    // Let's keep it Right for now or add a trigger here if we move it.
    
    // Track current sidebar state
    static QString currentSidebarId;

    // Connect Logic - VS Code style toggle behavior
    connect(activityBar_, &ActivityBar::activityChanged, this, [this](const QString& id){
        // Empty ID means "toggle off" - hide all sidebars
        if (id.isEmpty()) {
            if (projectDock) projectDock->hide();
            if (libraryDock) libraryDock->hide();
            currentSidebarId.clear();
            return;
        }

        // Hide all left docks first (exclusive sidebar)
        if (projectDock) projectDock->hide();
        if (libraryDock) libraryDock->hide();

        // Show the selected sidebar
        if (id == "explorer" && projectDock) {
            projectDock->show();
        }
        else if (id == "components" && libraryDock) {
            libraryDock->show();
        }
        else if (id == "search") {
            // TODO: Search Dock
        }

        currentSidebarId = id;
    });

    // Set Default Active
    activityBar_->setActive("explorer");
    currentSidebarId = "explorer";
    if (projectDock) projectDock->show();
}
