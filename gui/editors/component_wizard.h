#pragma once

#include <QWizard>
#include <QWizardPage>
#include <QLineEdit>
#include <QTextEdit>
#include <QListWidget>
#include <QComboBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QCheckBox>
#include <QMessageBox>
#include "circuit.h"

class SchematicEditor;

/**
 * ComponentWizard - A guided wizard for extracting a schematic as a reusable component.
 * 
 * This wizard guides the user through:
 * 1. Naming the component and providing a description.
 * 2. Selecting which nets become external pins.
 * 3. Choosing package type (visual representation).
 * 4. Saving to the component library.
 */
class ComponentWizard : public QWizard {
    Q_OBJECT
public:
    explicit ComponentWizard(SchematicEditor* editor, QWidget* parent = nullptr);
    
    // Get the resulting SubCircuitDefinition after wizard completes
    SubCircuitDefinition getResult() const { return resultDefinition; }
    
    // Check if the wizard completed successfully
    bool wasSuccessful() const { return success; }

private slots:
    void onFinished(int result);

private:
    SchematicEditor* schematicEditor;
    SubCircuitDefinition resultDefinition;
    bool success = false;
    
    // Wizard pages
    QWizardPage* createIntroPage();
    QWizardPage* createPinMappingPage();
    QWizardPage* createPackagePage();
    QWizardPage* createSummaryPage();
    
    // Widgets we need to access across pages
    QLineEdit* nameEdit;
    QTextEdit* descriptionEdit;
    QListWidget* pinList;
    QComboBox* packageCombo;
};

// ============================================================================
// PAGE 1: Component Info
// ============================================================================
class ComponentInfoPage : public QWizardPage {
    Q_OBJECT
public:
    ComponentInfoPage(QWidget* parent = nullptr);
    
    QString componentName() const { return nameEdit->text(); }
    QString componentDescription() const { return descriptionEdit->toPlainText(); }
    
private:
    QLineEdit* nameEdit;
    QTextEdit* descriptionEdit;
};

// ============================================================================
// PAGE 2: Pin Mapping
// ============================================================================
class PinMappingPage : public QWizardPage {
    Q_OBJECT
public:
    PinMappingPage(SchematicEditor* editor, QWidget* parent = nullptr);
    
    void initializePage() override;
    std::vector<PortInfo> getPortInfo() const;
    
private slots:
    void onAddPin();
    void onRemovePin();
    void onMoveUp();
    void onMoveDown();
    
private:
    SchematicEditor* schematicEditor;
    QListWidget* availableNodesList;
    QListWidget* selectedPinsList;
    QLineEdit* pinNameEdit;
};

// ============================================================================
// PAGE 3: Package Selection
// ============================================================================
class PackageSelectionPage : public QWizardPage {
    Q_OBJECT
public:
    PackageSelectionPage(QWidget* parent = nullptr);
    
    QString selectedPackage() const { return packageCombo->currentText(); }
    
private:
    QComboBox* packageCombo;
    QLabel* previewLabel;
};

// ============================================================================
// PAGE 4: Summary
// ============================================================================
class SummaryPage : public QWizardPage {
    Q_OBJECT
public:
    SummaryPage(QWidget* parent = nullptr);
    
    void initializePage() override;
    
private:
    QLabel* summaryLabel;
};
