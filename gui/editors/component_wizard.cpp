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
// PAGE 3: Package Selection & Preview Widget
// ============================================================================

#include <QPainter>
#include <QPaintEvent>

PackagePreviewWidget::PackagePreviewWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(200, 150);
    setStyleSheet("background: #1e1e1e; border: 1px solid #444; border-radius: 4px;");
}

void PackagePreviewWidget::setPackageType(const QString& type) {
    packageType = type;
    update();  // Trigger repaint
}

void PackagePreviewWidget::setPinCount(int count) {
    pinCount = count;
    update();
}

void PackagePreviewWidget::setPinNames(const QStringList& names) {
    pinNames = names;
    update();
}

void PackagePreviewWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    QRect r = rect().adjusted(10, 10, -10, -10);
    
    if (packageType.contains("DIP") || packageType.contains("Rectangle")) {
        drawDIP(painter, r);
    } else if (packageType.contains("QFN")) {
        drawQFN(painter, r);
    } else if (packageType.contains("BGA")) {
        drawBGA(painter, r);
    } else {
        drawGeneric(painter, r);
    }
}

void PackagePreviewWidget::drawDIP(QPainter& painter, const QRect& r) {
    // DIP package: Rectangle with pins on left and right
    int pinsPerSide = (pinCount + 1) / 2;
    int bodyWidth = r.width() * 0.5;
    int bodyHeight = r.height() * 0.8;
    
    QRect body(r.center().x() - bodyWidth/2, r.center().y() - bodyHeight/2, 
               bodyWidth, bodyHeight);
    
    // Draw body
    painter.setBrush(QColor("#3a3a3a"));
    painter.setPen(QPen(QColor("#888"), 2));
    painter.drawRoundedRect(body, 4, 4);
    
    // Draw notch (top center)
    painter.setBrush(QColor("#1e1e1e"));
    painter.drawEllipse(body.center().x() - 8, body.top() - 4, 16, 12);
    
    // Draw component name
    painter.setPen(QColor("#ccc"));
    painter.setFont(QFont("Monospace", 8));
    painter.drawText(body, Qt::AlignCenter, "IC");
    
    // Draw pins
    painter.setPen(QPen(QColor("#ffd700"), 2));
    int pinSpacing = bodyHeight / (pinsPerSide + 1);
    
    for (int i = 0; i < pinsPerSide && i < pinCount; ++i) {
        int y = body.top() + pinSpacing * (i + 1);
        
        // Left pins
        painter.drawLine(body.left() - 15, y, body.left(), y);
        painter.setPen(QColor("#aaa"));
        painter.setFont(QFont("Monospace", 7));
        QString leftLabel = (i < pinNames.size()) ? pinNames[i] : QString::number(i + 1);
        painter.drawText(body.left() - 45, y - 5, 30, 10, Qt::AlignRight, leftLabel);
        painter.setPen(QPen(QColor("#ffd700"), 2));
        
        // Right pins (numbered from bottom)
        int rightIdx = pinCount - 1 - i;
        if (rightIdx >= pinsPerSide) {
            painter.drawLine(body.right(), y, body.right() + 15, y);
            painter.setPen(QColor("#aaa"));
            QString rightLabel = (rightIdx < pinNames.size()) ? pinNames[rightIdx] : QString::number(rightIdx + 1);
            painter.drawText(body.right() + 18, y - 5, 30, 10, Qt::AlignLeft, rightLabel);
            painter.setPen(QPen(QColor("#ffd700"), 2));
        }
    }
}

void PackagePreviewWidget::drawQFN(QPainter& painter, const QRect& r) {
    // QFN: Square with pins on all four sides
    int size = qMin(r.width(), r.height()) * 0.7;
    QRect body(r.center().x() - size/2, r.center().y() - size/2, size, size);
    
    // Draw body
    painter.setBrush(QColor("#2d2d2d"));
    painter.setPen(QPen(QColor("#666"), 2));
    painter.drawRect(body);
    
    // Draw thermal pad
    int padSize = size * 0.4;
    QRect pad(body.center().x() - padSize/2, body.center().y() - padSize/2, padSize, padSize);
    painter.setBrush(QColor("#3a3a3a"));
    painter.drawRect(pad);
    
    // Draw corner marker
    painter.setBrush(QColor("#555"));
    painter.drawEllipse(body.topLeft().x() + 5, body.topLeft().y() + 5, 8, 8);
    
    // Draw pins on all sides
    int pinsPerSide = pinCount / 4;
    painter.setPen(QPen(QColor("#ffd700"), 1.5));
    
    for (int i = 0; i < pinsPerSide; ++i) {
        double ratio = (i + 0.5) / pinsPerSide;
        
        // Top
        int x = body.left() + size * ratio;
        painter.drawLine(x, body.top() - 8, x, body.top());
        
        // Bottom
        painter.drawLine(x, body.bottom(), x, body.bottom() + 8);
        
        // Left
        int y = body.top() + size * ratio;
        painter.drawLine(body.left() - 8, y, body.left(), y);
        
        // Right
        painter.drawLine(body.right(), y, body.right() + 8, y);
    }
    
    painter.setPen(QColor("#888"));
    painter.setFont(QFont("Monospace", 9));
    painter.drawText(body, Qt::AlignCenter, "QFN");
}

void PackagePreviewWidget::drawBGA(QPainter& painter, const QRect& r) {
    // BGA: Square with ball grid
    int size = qMin(r.width(), r.height()) * 0.7;
    QRect body(r.center().x() - size/2, r.center().y() - size/2, size, size);
    
    painter.setBrush(QColor("#2a2a2a"));
    painter.setPen(QPen(QColor("#555"), 2));
    painter.drawRect(body);
    
    // Draw ball grid
    int gridSize = 4;  // 4x4 grid for demo
    int spacing = size / (gridSize + 1);
    painter.setBrush(QColor("#888"));
    painter.setPen(Qt::NoPen);
    
    for (int row = 0; row < gridSize; ++row) {
        for (int col = 0; col < gridSize; ++col) {
            int x = body.left() + spacing * (col + 1);
            int y = body.top() + spacing * (row + 1);
            painter.drawEllipse(x - 4, y - 4, 8, 8);
        }
    }
    
    // Corner marker
    painter.setBrush(QColor("#ffd700"));
    painter.drawEllipse(body.left() + spacing - 4, body.top() + spacing - 4, 8, 8);
    
    painter.setPen(QColor("#666"));
    painter.setFont(QFont("Monospace", 8));
    painter.drawText(body.adjusted(0, 0, 0, -size/3), Qt::AlignCenter, "BGA");
}

void PackagePreviewWidget::drawGeneric(QPainter& painter, const QRect& r) {
    // Generic: Simple box symbol
    int w = r.width() * 0.5;
    int h = r.height() * 0.6;
    QRect body(r.center().x() - w/2, r.center().y() - h/2, w, h);
    
    painter.setBrush(QColor("#333"));
    painter.setPen(QPen(QColor("#777"), 2));
    painter.drawRect(body);
    
    painter.setPen(QColor("#aaa"));
    painter.setFont(QFont("Monospace", 10));
    painter.drawText(body, Qt::AlignCenter, QString("%1").arg(pinCount) + " pins");
}

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
    packageCombo->addItem("BGA (Ball Grid Array)");
    connect(packageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PackageSelectionPage::onPackageChanged);
    layout->addWidget(packageCombo);
    
    layout->addSpacing(10);
    
    // Live preview widget
    previewWidget = new PackagePreviewWidget(this);
    previewWidget->setMinimumHeight(180);
    layout->addWidget(previewWidget);
    
    // Description label
    descriptionLabel = new QLabel(this);
    descriptionLabel->setWordWrap(true);
    descriptionLabel->setStyleSheet("color: #888; font-size: 11px;");
    layout->addWidget(descriptionLabel);
    
    layout->addStretch();
    
    // Initialize with default
    onPackageChanged(0);
}

void PackageSelectionPage::onPackageChanged(int /*index*/) {
    QString pkg = packageCombo->currentText();
    previewWidget->setPackageType(pkg);
    
    // Extract pin count from package name
    QRegularExpression rx("(\\d+)");
    QRegularExpressionMatch match = rx.match(pkg);
    if (match.hasMatch()) {
        previewWidget->setPinCount(match.captured(1).toInt());
    } else {
        previewWidget->setPinCount(8);  // Default
    }
    
    // Update description
    if (pkg.contains("DIP")) {
        descriptionLabel->setText("Dual Inline Package - Classic through-hole IC format with pins on two sides.");
    } else if (pkg.contains("QFN")) {
        descriptionLabel->setText("Quad Flat No-leads - Compact surface-mount package with thermal pad.");
    } else if (pkg.contains("BGA")) {
        descriptionLabel->setText("Ball Grid Array - High-density package for advanced ICs.");
    } else if (pkg.contains("SOT")) {
        descriptionLabel->setText("Small Outline Transistor - Compact package for discrete devices.");
    } else {
        descriptionLabel->setText("Generic rectangular symbol with configurable pin count.");
    }
}

void PackageSelectionPage::setPinInfo(int count, const QStringList& names) {
    previewWidget->setPinCount(count);
    previewWidget->setPinNames(names);
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
