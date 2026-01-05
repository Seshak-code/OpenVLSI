#include "mainwindow.h"
#include <QApplication>
#include <QHeaderView>
#include <QStatusBar>
#include <QDebug>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("AcuteSim - Professional Circuit Designer");
    resize(1400, 900); // Decent default size for EDA
    
    // 1. Setup Central Area (Tabbed Workspace)
    setupCentralWidget();

    // 2. Setup Docks (Project, Properties, Messages)
    setupDocks();

    // 3. Setup Menus
    setupMenus();

    // 4. Status Bar
    statusBar()->showMessage("Ready");
}

void MainWindow::setupCentralWidget() {
    // Ideally this is a QTabWidget for multi-file support
    // For MVP, we stick the SchematicEditor directly here
    schematicEditor = new SchematicEditor(this);
    setCentralWidget(schematicEditor);
    
    // Connect Selection
    connect(schematicEditor, &SchematicEditor::itemSelected, this, &MainWindow::updateProperties);
    connect(schematicEditor, &SchematicEditor::wireSelected, this, &MainWindow::updateWireProperties);
}



void MainWindow::setupDocks() {
    // --- Project Browser (Left) ---
    createProjectDock();
    addDockWidget(Qt::LeftDockWidgetArea, projectDock);

    // --- Properties Inspector (Right) ---
    createPropertiesDock();
    addDockWidget(Qt::RightDockWidgetArea, propertiesDock);

    // --- Message Console (Bottom) ---
    createMessagesDock();
    addDockWidget(Qt::BottomDockWidgetArea, messagesDock);
}

void MainWindow::createProjectDock() {
    projectDock = new QDockWidget("Project Browser", this);
    projectDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    
    projectTree = new QTreeWidget(projectDock);
    projectTree->setHeaderLabel("Files");
    projectTree->setAlternatingRowColors(true);
    
    // Dummy Data
    auto* root = new QTreeWidgetItem(projectTree, {"MyDesign"});
    root->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_DirIcon));
    
    auto* schem = new QTreeWidgetItem(root, {"main_circuit.sch"});
    schem->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_FileIcon));
    
    auto* lib = new QTreeWidgetItem(root, {"models.lib"});
    lib->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_FileIcon));
    
    projectTree->expandAll();
    projectDock->setWidget(projectTree);
}

void MainWindow::createPropertiesDock() {
    propertiesDock = new QDockWidget("Properties", this);
    propertiesDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    
    propertiesTable = new QTableWidget(propertiesDock);
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

    propertiesDock->setWidget(propertiesTable);
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
        QTableWidgetItem* keyItem = new QTableWidgetItem(i.key());
        keyItem->setFlags(keyItem->flags() & ~Qt::ItemIsEditable); // Keys distinct
        propertiesTable->setItem(row, 0, keyItem);
        propertiesTable->setItem(row, 1, new QTableWidgetItem(i.value()));
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
    statusBar()->showMessage("Running Simulation...");
    // Ideally call schematicEditor->toNetlist() and pass to solver
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
