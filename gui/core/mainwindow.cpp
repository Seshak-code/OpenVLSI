#include "mainwindow.h"
#include "welcome_widget.h"
#include <QApplication>
#include <QHeaderView>
#include <QStatusBar>
#include <QDebug>
#include <QComboBox>
#include <QMessageBox>
#include <QPushButton>
#include <QLabel>
#include <QInputDialog>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("AcuteSim - ASIC Design Suite");
    resize(1280, 720);
    
    // Initialize Design Flow Manager
    flowManager = new DesignFlowManager(this);
    connect(flowManager, &DesignFlowManager::stageChanged, this, &MainWindow::onStageChanged);
    
    // 1. Setup Central Area (Stage-based Workspace)
    setupCentralWidget();

    // 2. Setup Docks (Project, Properties, Messages)
    setupDocks();

    // 3. Setup Menus and Toolbar
    setupMenus();
    setupStageToolbar();

    // 4. Status Bar
    statusBar()->showMessage("Ready - RTL Design Stage");
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
    simulationWorkspace = new SimulationWorkspace(this);
    simulationWorkspace->setSchematicEditor(schematicEditor);
    // Note: NOT added to stageTabs - accessed via "Set Simulation" menu
    
    // Connect simulation logs to bottom panels
    connect(simulationWorkspace, &SimulationWorkspace::logMessage, this, [this](const QString& msg) {
        if (outputLog_) outputLog_->appendPlainText(msg);
    });
    connect(simulationWorkspace, &SimulationWorkspace::streamMessage, this, [this](const QString& msg) {
        if (streamingLog_) streamingLog_->appendPlainText(msg);
    });
    connect(simulationWorkspace, &SimulationWorkspace::resultsReady, this, &MainWindow::onSimulationResults);
    
    connect(simulationWorkspace, &SimulationWorkspace::simulationStarted, this, [this](const QString&) {
        // Clear panels before simulation run
        if (outputLog_) outputLog_->clear();
        if (streamingLog_) streamingLog_->clear();
        if (problemsPanel) problemsPanel->clear();
        if (resultsTree_) resultsTree_->clear();
        if (waveformLog_) waveformLog_->clear();
        
        // Auto-focus Streaming tab for visibility
        if (streamingDock_) {
            streamingDock_->raise();
            streamingDock_->show();
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
            schematicEditor = qobject_cast<SchematicEditor*>(rtlPageTabs->widget(idx));
            // Update UI to match current page's mode
            if (schematicEditor && libraryPanel) {
                libraryPanel->setMode(schematicEditor->getMode());
            }
        }
    });
    
    // Connect schematic selection signals
    connect(schematicEditor, &SchematicEditor::itemSelected, this, &MainWindow::updateProperties);
    connect(schematicEditor, &SchematicEditor::wireSelected, this, &MainWindow::updateWireProperties);
}



void MainWindow::setupDocks() {
    // --- Project Browser (Left) ---
    createProjectDock();
    addDockWidget(Qt::LeftDockWidgetArea, projectDock);

    // --- Component Library (Left, Tabbed or Below) ---
    createLibraryDock();
    addDockWidget(Qt::LeftDockWidgetArea, libraryDock);
    tabifyDockWidget(projectDock, libraryDock);
    
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

    // --- Message Console (Bottom) ---
    createMessagesDock();
    addDockWidget(Qt::BottomDockWidgetArea, messagesDock);
    
    // --- Debug Panels (Bottom, Tabbed with Problems) ---
    createOutputDock();
    addDockWidget(Qt::BottomDockWidgetArea, outputDock_);
    tabifyDockWidget(messagesDock, outputDock_);
    
    createStreamingDock();
    addDockWidget(Qt::BottomDockWidgetArea, streamingDock_);
    tabifyDockWidget(outputDock_, streamingDock_);
    
    createResultsDock();
    addDockWidget(Qt::BottomDockWidgetArea, resultsDock_);
    tabifyDockWidget(streamingDock_, resultsDock_);
    
    createWaveformDock();
    addDockWidget(Qt::BottomDockWidgetArea, waveformDock_);
    tabifyDockWidget(resultsDock_, waveformDock_);
    
    // Phase 12.3: Worker Status
    createWorkerDock();
    addDockWidget(Qt::BottomDockWidgetArea, workerDock);
    tabifyDockWidget(waveformDock_, workerDock);
    
    // Make Problems tab active by default
    messagesDock->raise();
}

void MainWindow::createAiDock() {
    aiDock = new QDockWidget("Copilot Assistant", this);
    aiDock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
    
    aiPanel = new AiAssistantPanel(schematicEditor, this);
    aiDock->setWidget(aiPanel);
}

void MainWindow::createProjectDock() {
    projectDock = new QDockWidget("Project Browser", this);
    projectDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    
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
    
    libraryPanel = new ComponentLibrary(this);
    
    // Connect Selection
    connect(libraryPanel, &ComponentLibrary::componentSelected, this, [this](ComponentType type){
         schematicEditor->setTool(type);
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
            
            connect(combo, &QComboBox::currentTextChanged, this, [this, item, key](const QString& text){
                // Safety check: ensure item is still valid (simplified)
                if (item && activeComponent == item) {
                   item->params[key] = text;
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
    
    if (activeComponent) {
        if (col == 1) { // Value column
            if (row == 0) {
                activeComponent->name = item->text();
            } else if (row == 1) {
                activeComponent->value = item->text();
            } else if (row > 2) {
                // Dynamic Param
                QString key = propertiesTable->item(row, 0)->text();
                activeComponent->params[key] = item->text();
            }
        }
    } 
    else if (activeWire) {
        if (col == 1 && row == 0) {
            // Renaming Net
            schematicEditor->setWireName(activeWire, item->text());
        }
    }
}

void MainWindow::createMessagesDock() {
    messagesDock = new QDockWidget("Problems", this);
    messagesDock->setAllowedAreas(Qt::BottomDockWidgetArea);
    
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
    outputDock_->setAllowedAreas(Qt::BottomDockWidgetArea);
    
    outputLog_ = new QPlainTextEdit(outputDock_);
    outputLog_->setReadOnly(true);
    outputLog_->setStyleSheet("background: #1e1e1e; color: #ccc; font-family: monospace;");
    outputDock_->setWidget(outputLog_);
}

void MainWindow::createStreamingDock() {
    streamingDock_ = new QDockWidget("Streaming", this);
    streamingDock_->setAllowedAreas(Qt::BottomDockWidgetArea);
    
    streamingLog_ = new QPlainTextEdit(streamingDock_);
    streamingLog_->setReadOnly(true);
    streamingLog_->setStyleSheet("background: #1e1e1e; color: #4ec9b0; font-family: monospace;");
    streamingDock_->setWidget(streamingLog_);
}

void MainWindow::createResultsDock() {
    resultsDock_ = new QDockWidget("Results", this);
    resultsDock_->setAllowedAreas(Qt::BottomDockWidgetArea);
    
    resultsTree_ = new QTreeWidget(resultsDock_);
    resultsTree_->setHeaderLabels({"Node", "Value"});
    resultsTree_->setColumnWidth(0, 150);
    resultsTree_->setStyleSheet("QTreeWidget { background: #1e1e1e; color: #ccc; border: none; } "
                               "QHeaderView::section { background: #2d2d2d; color: #888; border: none; padding: 4px; }");
    resultsDock_->setWidget(resultsTree_);
}

void MainWindow::createWaveformDock() {
    waveformDock_ = new QDockWidget("Waveforms", this);
    waveformDock_->setAllowedAreas(Qt::BottomDockWidgetArea);
    
    waveformLog_ = new QPlainTextEdit(waveformDock_);
    waveformLog_->setReadOnly(true);
    waveformLog_->setStyleSheet("background: #1e1e1e; color: #4ec9b0; font-family: monospace;");
    waveformLog_->setPlaceholderText("Waveform data points will define trace here...");
    waveformDock_->setWidget(waveformLog_);
}

void MainWindow::onSimulationResults(const QJsonObject& results) {
    QString type = results["type"].toString();
    
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
        // Force switch to Results tab logic (not possible directly with QDockWidget tabify logic without storing tabRef, 
        // but user sees it populate)
    } 
    else if (type == "TransientPoint") {
        if (waveformLog_) {
            double time = results["time"].toDouble();
            QJsonArray voltages = results["voltages"].toArray();
            QString line = QString("t=%1u: ").arg(time * 1e6, 0, 'f', 2);
            for (int i = 0; i < qMin(voltages.size(), 4); ++i) { // Show first 4 nodes
                line += QString("V(%1)=%2 ").arg(i+1).arg(voltages[i].toDouble(), 0, 'f', 2);
            }
            if (voltages.size() > 4) line += "...";
            waveformLog_->appendPlainText(line);
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
    if (!simulationWorkspace) return;

    // Check if simulationWorkspace is already in a visible dialog
    QWidget* parent = simulationWorkspace->parentWidget();
    if (parent && parent->isWindow()) {
        parent->show();
        parent->raise();
        parent->activateWindow();
        return;
    }

    // Create a wrapper dialog
    QDialog* simDialog = new QDialog(this);
    simDialog->setWindowTitle("Simulation Workspace");
    simDialog->resize(1100, 700); // Larger default size for workspace
    simDialog->setAttribute(Qt::WA_DeleteOnClose);
    
    QVBoxLayout* layout = new QVBoxLayout(simDialog);
    layout->setContentsMargins(0, 0, 0, 0);
    
    // Move the member workspace into the dialog
    layout->addWidget(simulationWorkspace);
    simulationWorkspace->show();
    
    // Ensure the schematic editor link is fresh
    if (schematicEditor) {
        simulationWorkspace->setSchematicEditor(schematicEditor);
    }
    
    // When dialog closes, reclaim the workspace so it isn't destroyed
    connect(simDialog, &QDialog::finished, this, [this, simDialog]() {
        if (simulationWorkspace) {
            simulationWorkspace->setParent(this); // Reparent back to MainWindow
            simulationWorkspace->hide();
        }
    });
    
    // Close button logic handled by window chrome, but we can add one if needed?
    // The workspace has its own toolbar, so window chrome is sufficient.
    
    simDialog->show();
}

void MainWindow::closeEvent(QCloseEvent *event) {
    // Save layout state here
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
    
    // Sync Buttons (Phase 14.2)
    QAction* genVerilog = new QAction("Sch -> HDL", this);
    genVerilog->setToolTip("Generate Verilog-AMS from Schematic");
    connect(genVerilog, &QAction::triggered, this, &MainWindow::syncSchematicToVerilog);
    modeToolbar->addAction(genVerilog);
    
    QAction* parseVerilog = new QAction("HDL -> Sch", this);
    parseVerilog->setToolTip("Update Schematic from Verilog-AMS (Ghost Placement)");
    connect(parseVerilog, &QAction::triggered, this, &MainWindow::syncVerilogToSchematic);
    modeToolbar->addAction(parseVerilog);
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
    QWidget* current = rtlPageTabs->widget(index);
    if (auto* editor = qobject_cast<SchematicEditor*>(current)) {
        schematicEditor = editor;
        if (libraryPanel) libraryPanel->setMode(editor->getMode());
        if (aiPanel) aiPanel->setEditor(editor);
        if (simulationWorkspace) simulationWorkspace->setSchematicEditor(editor);
        statusBar()->showMessage(QString("Active: %1").arg(rtlPageTabs->tabText(index)));
    } else {
        // Welcome widget or placeholder - no active editor
        schematicEditor = nullptr;
        if (aiPanel) aiPanel->setEditor(nullptr);
        if (simulationWorkspace) simulationWorkspace->setSchematicEditor(nullptr);
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

void MainWindow::openSchematicTab(const QString& title, const QJsonObject& content) {
    SchematicEditor* newEditor = new SchematicEditor(this);
    
    // Auto-detect mode from content logic or default to Bench (Harness is usually Bench-like)
    // For auto-harness, it uses Bench Mode components (Sources, Probes).
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

void MainWindow::createWorkerDock() {
    workerDock = new QDockWidget("Worker Status", this);
    workerDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::RightDockWidgetArea);
    workerPanel = new acutesim::gui::orchestration::WorkerStatusPanel(workerDock);
    workerDock->setWidget(workerPanel);
}
