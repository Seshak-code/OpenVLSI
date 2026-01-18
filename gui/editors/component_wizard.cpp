#include "component_wizard.h"
#include "schematic_editor.h"
#include <QGridLayout>
#include <QRegularExpression>

// ============================================================================
// WIZARD MAIN CLASS
// ============================================================================

ComponentWizard::ComponentWizard(SchematicEditor* editor, QWidget* parent)
    : QWizard(parent), schematicEditor(editor)
{
    setWindowTitle("Extract as Component");
    setWizardStyle(QWizard::ModernStyle);
    setMinimumSize(600, 500);
    
    // Add pages
    addPage(createIntroPage());
    addPage(createPinMappingPage());
    addPage(createPackagePage());
    addPage(createSummaryPage());
    
    connect(this, &QWizard::finished, this, &ComponentWizard::onFinished);
}

QWizardPage* ComponentWizard::createIntroPage() {
    auto* page = new ComponentInfoPage(this);
    return page;
}

QWizardPage* ComponentWizard::createPinMappingPage() {
    auto* page = new PinMappingPage(schematicEditor, this);
    return page;
}

QWizardPage* ComponentWizard::createPackagePage() {
    auto* page = new PackageSelectionPage(this);
    return page;
}

QWizardPage* ComponentWizard::createSummaryPage() {
    auto* page = new SummaryPage(this);
    return page;
}

void ComponentWizard::onFinished(int result) {
    if (result == QDialog::Accepted) {
        // Gather all data and create the SubCircuitDefinition
        auto* infoPage = qobject_cast<ComponentInfoPage*>(page(0));
        auto* pinPage = qobject_cast<PinMappingPage*>(page(1));
        auto* packagePage = qobject_cast<PackageSelectionPage*>(page(2));
        
        if (infoPage && pinPage && schematicEditor) {
            TensorNetlist netlist = schematicEditor->toNetlist();
            std::vector<PortInfo> ports = pinPage->getPortInfo();
            
            resultDefinition = extractSubCircuit(
                netlist,
                infoPage->componentName().toStdString(),
                infoPage->componentDescription().toStdString(),
                ports
            );
            
            success = true;
        }
    }
}

// ============================================================================
// PAGE 1: Component Info
// ============================================================================

ComponentInfoPage::ComponentInfoPage(QWidget* parent) : QWizardPage(parent) {
    setTitle("Component Information");
    setSubTitle("Enter a name and description for your new component.");
    
    auto* layout = new QFormLayout(this);
    
    nameEdit = new QLineEdit(this);
    nameEdit->setPlaceholderText("e.g., MyInverter, VoltageRegulator");
    layout->addRow("Component Name:", nameEdit);
    
    descriptionEdit = new QTextEdit(this);
    descriptionEdit->setPlaceholderText("Describe what this component does...");
    descriptionEdit->setMaximumHeight(100);
    layout->addRow("Description:", descriptionEdit);
    
    // Register fields for wizard navigation
    registerField("componentName*", nameEdit);
}

// ============================================================================
// PAGE 2: Pin Mapping
// ============================================================================

PinMappingPage::PinMappingPage(SchematicEditor* editor, QWidget* parent)
    : QWizardPage(parent), schematicEditor(editor)
{
    setTitle("Pin Mapping");
    setSubTitle("Select which circuit nodes become external pins of your component.");
    
    auto* mainLayout = new QHBoxLayout(this);
    
    // Left: Available Nodes
    auto* leftPanel = new QVBoxLayout();
    leftPanel->addWidget(new QLabel("Available Nodes:"));
    availableNodesList = new QListWidget(this);
    availableNodesList->setSelectionMode(QAbstractItemView::SingleSelection);
    leftPanel->addWidget(availableNodesList);
    mainLayout->addLayout(leftPanel);
    
    // Center: Add/Remove buttons
    auto* centerPanel = new QVBoxLayout();
    centerPanel->addStretch();
    
    pinNameEdit = new QLineEdit(this);
    pinNameEdit->setPlaceholderText("Pin name");
    pinNameEdit->setMaximumWidth(100);
    centerPanel->addWidget(pinNameEdit);
    
    auto* addBtn = new QPushButton("→ Add", this);
    connect(addBtn, &QPushButton::clicked, this, &PinMappingPage::onAddPin);
    centerPanel->addWidget(addBtn);
    
    auto* removeBtn = new QPushButton("← Remove", this);
    connect(removeBtn, &QPushButton::clicked, this, &PinMappingPage::onRemovePin);
    centerPanel->addWidget(removeBtn);
    
    centerPanel->addStretch();
    
    auto* upBtn = new QPushButton("↑ Up", this);
    connect(upBtn, &QPushButton::clicked, this, &PinMappingPage::onMoveUp);
    centerPanel->addWidget(upBtn);
    
    auto* downBtn = new QPushButton("↓ Down", this);
    connect(downBtn, &QPushButton::clicked, this, &PinMappingPage::onMoveDown);
    centerPanel->addWidget(downBtn);
    
    centerPanel->addStretch();
    mainLayout->addLayout(centerPanel);
    
    // Right: Selected Pins (in order)
    auto* rightPanel = new QVBoxLayout();
    rightPanel->addWidget(new QLabel("Component Pins (in order):"));
    selectedPinsList = new QListWidget(this);
    rightPanel->addWidget(selectedPinsList);
    mainLayout->addLayout(rightPanel);
}

void PinMappingPage::initializePage() {
    availableNodesList->clear();
    selectedPinsList->clear();
    
    if (!schematicEditor) return;
    
    TensorNetlist netlist = schematicEditor->toNetlist();
    
    // Add all non-ground nodes
    for (int i = 1; i <= netlist.numGlobalNodes; ++i) {
        availableNodesList->addItem(QString("Node %1").arg(i));
    }
    
    // Add ground as a special entry
    availableNodesList->addItem("Node 0 (GND)");
}

void PinMappingPage::onAddPin() {
    auto* selected = availableNodesList->currentItem();
    if (!selected) return;
    
    QString pinName = pinNameEdit->text().trimmed();
    if (pinName.isEmpty()) {
        pinName = QString("PIN%1").arg(selectedPinsList->count() + 1);
    }
    
    // Format: "PIN_NAME -> Node X"
    QString entry = QString("%1 → %2").arg(pinName).arg(selected->text());
    selectedPinsList->addItem(entry);
    
    // Clear pin name for next entry
    pinNameEdit->clear();
}

void PinMappingPage::onRemovePin() {
    auto* selected = selectedPinsList->currentItem();
    if (selected) {
        delete selectedPinsList->takeItem(selectedPinsList->row(selected));
    }
}

void PinMappingPage::onMoveUp() {
    int row = selectedPinsList->currentRow();
    if (row > 0) {
        auto* item = selectedPinsList->takeItem(row);
        selectedPinsList->insertItem(row - 1, item);
        selectedPinsList->setCurrentRow(row - 1);
    }
}

void PinMappingPage::onMoveDown() {
    int row = selectedPinsList->currentRow();
    if (row >= 0 && row < selectedPinsList->count() - 1) {
        auto* item = selectedPinsList->takeItem(row);
        selectedPinsList->insertItem(row + 1, item);
        selectedPinsList->setCurrentRow(row + 1);
    }
}

std::vector<PortInfo> PinMappingPage::getPortInfo() const {
    std::vector<PortInfo> ports;
    
    for (int i = 0; i < selectedPinsList->count(); ++i) {
        QString text = selectedPinsList->item(i)->text();
        // Parse "PIN_NAME → Node X"
        QStringList parts = text.split(" → ");
        if (parts.size() >= 2) {
            PortInfo port;
            port.pinName = parts[0].toStdString();
            
            // Extract node number
            QString nodePart = parts[1];
            if (nodePart.contains("GND")) {
                port.globalNodeId = 0;
                port.isPowerPin = true;
            } else {
                // "Node X" -> extract X
                QRegularExpression rx("Node (\\d+)");
                QRegularExpressionMatch match = rx.match(nodePart);
                if (match.hasMatch()) {
                    port.globalNodeId = match.captured(1).toInt();
                }
            }
            
            // Check if it's a power pin by name
            QString upperName = parts[0].toUpper();
            if (upperName.contains("VDD") || upperName.contains("VCC") || 
                upperName.contains("VSS") || upperName.contains("GND")) {
                port.isPowerPin = true;
            }
            
            ports.push_back(port);
        }
    }
    
    return ports;
}

// ============================================================================
// PAGE 3: Package Selection
// ============================================================================

PackageSelectionPage::PackageSelectionPage(QWidget* parent) : QWizardPage(parent) {
    setTitle("Package Selection");
    setSubTitle("Choose how your component will appear in the schematic.");
    
    auto* layout = new QVBoxLayout(this);
    
    layout->addWidget(new QLabel("Symbol Style:"));
    
    packageCombo = new QComboBox(this);
    packageCombo->addItem("Rectangle (Generic IC)");
    packageCombo->addItem("DIP-8 (8-pin)");
    packageCombo->addItem("DIP-14 (14-pin)");
    packageCombo->addItem("DIP-16 (16-pin)");
    packageCombo->addItem("SOT-23 (3-pin)");
    packageCombo->addItem("SOT-223 (4-pin)");
    packageCombo->addItem("QFN-16 (16-pin)");
    layout->addWidget(packageCombo);
    
    layout->addSpacing(20);
    
    previewLabel = new QLabel(this);
    previewLabel->setMinimumHeight(150);
    previewLabel->setAlignment(Qt::AlignCenter);
    previewLabel->setStyleSheet("background: #2a2a2a; border: 1px solid #555; border-radius: 5px;");
    previewLabel->setText("[Package Preview Coming Soon]");
    layout->addWidget(previewLabel);
    
    layout->addStretch();
}

// ============================================================================
// PAGE 4: Summary
// ============================================================================

SummaryPage::SummaryPage(QWidget* parent) : QWizardPage(parent) {
    setTitle("Summary");
    setSubTitle("Review your component before saving.");
    
    auto* layout = new QVBoxLayout(this);
    
    summaryLabel = new QLabel(this);
    summaryLabel->setWordWrap(true);
    summaryLabel->setStyleSheet("font-size: 12px; padding: 10px; background: #1e1e1e; border-radius: 5px;");
    layout->addWidget(summaryLabel);
    
    layout->addStretch();
}

void SummaryPage::initializePage() {
    // Gather info from previous pages
    auto* infoPage = qobject_cast<ComponentInfoPage*>(wizard()->page(0));
    auto* pinPage = qobject_cast<PinMappingPage*>(wizard()->page(1));
    auto* packagePage = qobject_cast<PackageSelectionPage*>(wizard()->page(2));
    
    QString summary;
    summary += QString("<h3>%1</h3>").arg(infoPage ? infoPage->componentName() : "Unknown");
    summary += QString("<p><i>%1</i></p>").arg(infoPage ? infoPage->componentDescription() : "");
    summary += QString("<p><b>Package:</b> %1</p>").arg(packagePage ? packagePage->selectedPackage() : "Unknown");
    
    if (pinPage) {
        auto ports = pinPage->getPortInfo();
        summary += QString("<p><b>Pins:</b> %1</p><ul>").arg(ports.size());
        for (const auto& p : ports) {
            summary += QString("<li>%1 (Node %2)%3</li>")
                .arg(QString::fromStdString(p.pinName))
                .arg(p.globalNodeId)
                .arg(p.isPowerPin ? " ⚡" : "");
        }
        summary += "</ul>";
    }
    
    summary += "<p style='color: #4CAF50;'>Click <b>Finish</b> to add this component to your library.</p>";
    
    summaryLabel->setText(summary);
}
