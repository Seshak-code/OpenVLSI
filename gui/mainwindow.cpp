#include "mainwindow.h"
#include <QApplication>
#include <QHeaderView>
#include <QStatusBar>
#include <QDebug>
#include <QComboBox>
#include <QMessageBox>
#include <QPushButton>
#include <QLabel>

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
    
    // Initial schematic page
    schematicEditor = new SchematicEditor(this);
    rtlPageTabs->addTab(schematicEditor, "top.sch");
    
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
    
    // Simulator Panel (can be shown in any stage)
    simulatorPanel = new SimulatorWindow(this);
    stageTabs->addTab(simulatorPanel, "Simulation");
    
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
    messagesDock = new QDockWidget("Messages", this);
    messagesDock->setAllowedAreas(Qt::BottomDockWidgetArea);
    
    messageTable = new QTableWidget(messagesDock);
    messageTable->setColumnCount(3);
    messageTable->setHorizontalHeaderLabels({"Time", "Type", "Message"});
    messageTable->horizontalHeader()->setStretchLastSection(true);
    messageTable->verticalHeader()->setVisible(false);
    
    messageTable->setRowCount(1);
    messageTable->setItem(0, 0, new QTableWidgetItem("10:00:00"));
    messageTable->setItem(0, 1, new QTableWidgetItem("Info"));
    messageTable->setItem(0, 2, new QTableWidgetItem("Application started."));

    messagesDock->setWidget(messageTable);
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

    // Simulation
    QMenu* simMenu = mb->addMenu("&Simulation");
    simMenu->addAction("Run Simulation", this, &MainWindow::runSimulation);
}

void MainWindow::newProject() {
    statusBar()->showMessage("New Project created.");
}

void MainWindow::openProject() {
    statusBar()->showMessage("Open Project dialog...");
}

void MainWindow::runSimulation() {
    statusBar()->showMessage("Running ERC...");
    
    // Run ERC
    QStringList ercErrors = schematicEditor->runERC();
    if (!ercErrors.isEmpty()) {
        QString msg = "ERC Issues Found:\n" + ercErrors.join("\n");
        QMessageBox::warning(this, "ERC Warnings", msg);
    }
    
    statusBar()->showMessage("Generating Netlist...");
    TensorNetlist netlist = schematicEditor->toNetlist();
    statusBar()->showMessage(QString("Netlist Generated: %1 nodes").arg(netlist.numGlobalNodes));

    // Pass netlist to simulator panel and switch tab
    simulatorPanel->setNetlist(netlist); 
    stageTabs->setCurrentWidget(simulatorPanel);
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
    
    // Stage Selector Combobox
    QComboBox* stageCombo = new QComboBox();
    stageCombo->addItems({"RTL Design", "Synthesis", "Physical Design", "Verification"});
    stageCombo->setToolTip("Select design flow stage");
    stageCombo->setMinimumWidth(150);
    
    connect(stageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        if (stageTabs) {
            stageTabs->setCurrentIndex(idx);
        }
    });
    
    // Sync combo with tab changes
    connect(stageTabs, &QTabWidget::currentChanged, stageCombo, &QComboBox::setCurrentIndex);
    
    modeToolbar->addWidget(new QLabel(" Stage: "));
    modeToolbar->addWidget(stageCombo);
    modeToolbar->addSeparator();
    
    // Mode Toggle (Lab/System) - Keep for simulation mode within stages
    toggleModeAction = new QAction("System Integration", this);
    toggleModeAction->setCheckable(true);
    toggleModeAction->setChecked(true);
    toggleModeAction->setToolTip("Solver mode: Ideal (Lab) vs Realistic (System)");
    connect(toggleModeAction, &QAction::toggled, this, &MainWindow::onModeToggle);
    modeToolbar->addAction(toggleModeAction);
    
    // Initialize
    onModeToggle();
    
    modeToolbar->addSeparator();
    
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
    static int pageCount = 1; // Simple counter for unique names
    QString pageName = QString("module_%1.sch").arg(++pageCount);
    
    SchematicEditor* newEditor = new SchematicEditor(this);
    rtlPageTabs->addTab(newEditor, pageName);
    rtlPageTabs->setCurrentWidget(newEditor);
    
    // Connect signals for the new editor
    connect(newEditor, &SchematicEditor::itemSelected, this, &MainWindow::updateProperties);
    connect(newEditor, &SchematicEditor::wireSelected, this, &MainWindow::updateWireProperties);
    
    statusBar()->showMessage(QString("Added new schematic page: %1").arg(pageName), 3000);
}

void MainWindow::onModeToggle() {
    if (toggleModeAction->isChecked()) {
        setMode(AppMode::SYSTEM_INTEGRATION);
    } else {
        setMode(AppMode::LAB_BENCHMARK);
    }
}

void MainWindow::setMode(AppMode mode) {
    currentMode = mode;
    
    if (mode == AppMode::SYSTEM_INTEGRATION) {
        toggleModeAction->setText("System Integration");
        toggleModeAction->setToolTip("Simulation: Real PDN (ESR/Ripple) + High Precision Solver");
    } else {
        toggleModeAction->setText("Lab Benchmark");
        toggleModeAction->setToolTip("Simulation: Ideal Sources (0 Resistance) + Fast Solver");
    }
}
