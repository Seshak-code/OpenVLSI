// ============================================================================
// headless_test_stubs.cpp — Minimal stubs for test_gui_headless_scenarios
// ============================================================================
// Provides symbols referenced by schematic_editor.cpp that we don't need
// in headless mode. These are never called at runtime in headless scenarios.
// ============================================================================

// Suppress unused-parameter warnings in stubs
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

#include <QWidget>
#include <QJsonObject>
#include <QString>
#include <QWizard>

// ── ComponentWizard stub ─────────────────────────────────────────────────────
// Used in schematic_editor.cpp "Extract Component" handler — never called headless.
#include "../../gui/editors/component_wizard.h"

class SchematicEditor;

ComponentWizard::ComponentWizard(SchematicEditor* editor, QWidget* parent)
    : QWizard(parent), schematicEditor(editor), nameEdit(nullptr),
      descriptionEdit(nullptr), pinList(nullptr), packageCombo(nullptr),
      success(false) {}

void ComponentWizard::onFinished(int) {}
QWizardPage* ComponentWizard::createIntroPage() { return nullptr; }
QWizardPage* ComponentWizard::createPinMappingPage() { return nullptr; }
QWizardPage* ComponentWizard::createPackagePage() { return nullptr; }
QWizardPage* ComponentWizard::createSummaryPage() { return nullptr; }

ComponentInfoPage::ComponentInfoPage(QWidget* parent) : QWizardPage(parent), nameEdit(nullptr), descriptionEdit(nullptr) {}

PinMappingPage::PinMappingPage(SchematicEditor* editor, QWidget* parent)
    : QWizardPage(parent), schematicEditor(editor),
      availableNodesList(nullptr), selectedPinsList(nullptr), pinNameEdit(nullptr) {}
void PinMappingPage::initializePage() {}
QList<QPair<QString, QString>> PinMappingPage::getPortInfo() const { return {}; }
void PinMappingPage::onAddPin() {}
void PinMappingPage::onRemovePin() {}
void PinMappingPage::onMoveUp() {}
void PinMappingPage::onMoveDown() {}

PackagePreviewWidget::PackagePreviewWidget(QWidget* parent) : QWidget(parent) {}
void PackagePreviewWidget::setPackageType(const QString&) {}
void PackagePreviewWidget::setPinCount(int) {}
void PackagePreviewWidget::setPinNames(const QStringList&) {}
void PackagePreviewWidget::paintEvent(QPaintEvent*) {}
void PackagePreviewWidget::drawDIP(QPainter&, const QRect&) {}
void PackagePreviewWidget::drawQFN(QPainter&, const QRect&) {}
void PackagePreviewWidget::drawBGA(QPainter&, const QRect&) {}
void PackagePreviewWidget::drawGeneric(QPainter&, const QRect&) {}

PackageSelectionPage::PackageSelectionPage(QWidget* parent) : QWizardPage(parent), packageCombo(nullptr), previewWidget(nullptr), descriptionLabel(nullptr) {}
void PackageSelectionPage::setPinInfo(int, const QStringList&) {}
void PackageSelectionPage::onPackageChanged(int) {}

SummaryPage::SummaryPage(QWidget* parent) : QWizardPage(parent), summaryLabel(nullptr) {}
void SummaryPage::initializePage() {}

#pragma GCC diagnostic pop
