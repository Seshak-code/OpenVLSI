#include "signoff_result_dialog.h"
#include <QHeaderView>
#include <QGroupBox>
#include <QFormLayout>

namespace acutesim::gui::orchestration {

using namespace acutesim::compute::orchestration;

// ============================================================================
// COMMON LAYOUT
// ============================================================================

void SignOffResultDialog::setupCommonLayout() {
    setMinimumSize(700, 500);
    setStyleSheet(
        "QDialog { background-color: #1e1e1e; }"
        "QLabel { color: #ccc; }"
        "QTabWidget::pane { border: 1px solid #444; background: #252526; }"
        "QTabBar::tab { background: #2d2d2d; color: #aaa; padding: 8px 16px; }"
        "QTabBar::tab:selected { background: #1e1e1e; color: #dcdcaa; }"
        "QTableWidget { background: #1e1e1e; color: #ccc; gridline-color: #333; "
        "  alternate-background-color: #252526; border: none; }"
        "QTableWidget::item { padding: 4px; }"
        "QHeaderView::section { background: #2d2d2d; color: #aaa; "
        "  padding: 6px; border: 1px solid #444; }"
        "QTextEdit { background: #1e1e1e; color: #b5cea8; font-family: 'Menlo', monospace; "
        "  font-size: 12px; border: none; }"
        "QPushButton { background: #0e639c; color: white; border: none; "
        "  padding: 6px 16px; border-radius: 4px; }"
        "QPushButton:hover { background: #1177bb; }");

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(12, 12, 12, 12);

    m_tabs = new QTabWidget(this);
    m_mainLayout->addWidget(m_tabs);

    // Close button
    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    auto* closeBtn = new QPushButton("Close", this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    btnLayout->addWidget(closeBtn);
    m_mainLayout->addLayout(btnLayout);
}

// ============================================================================
// DRC CONSTRUCTOR
// ============================================================================

SignOffResultDialog::SignOffResultDialog(const DRCResult& result, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Sign-Off Results — DRC");
    setupCommonLayout();
    buildDRCView(result);
}

void SignOffResultDialog::buildDRCView(const DRCResult& result) {
    // --- Summary Tab ---
    auto* summaryWidget = new QWidget();
    auto* summaryLayout = new QVBoxLayout(summaryWidget);
    summaryLayout->setSpacing(12);

    // Traffic light
    auto* statusLabel = new QLabel(this);
    if (result.cleanDRC) {
        statusLabel->setText("✅  DRC CLEAN — No violations found");
        statusLabel->setStyleSheet("color: #4ec9b0; font-size: 18px; font-weight: bold; padding: 12px;");
    } else {
        statusLabel->setText(QString("❌  DRC FAILED — %1 violation(s)")
            .arg(result.totalViolations));
        statusLabel->setStyleSheet("color: #f44747; font-size: 18px; font-weight: bold; padding: 12px;");
    }
    summaryLayout->addWidget(statusLabel);

    // Stats
    auto* statsForm = new QFormLayout();
    statsForm->setLabelAlignment(Qt::AlignRight);
    auto addStat = [&](const QString& label, const QString& value) {
        auto* v = new QLabel(value, this);
        v->setStyleSheet("color: #dcdcaa; font-weight: bold;");
        statsForm->addRow(new QLabel(label, this), v);
    };
    addStat("Total Violations:", QString::number(result.totalViolations));
    addStat("Errors:", QString::number(result.errorCount));
    addStat("Warnings:", QString::number(result.warningCount));
    addStat("Rules Checked:", QString::number(result.violations.size()));
    summaryLayout->addLayout(statsForm);
    summaryLayout->addStretch();

    m_tabs->addTab(summaryWidget, "Summary");

    // --- Violations Table Tab ---
    if (!result.violations.isEmpty()) {
        auto* table = new QTableWidget(result.violations.size(), 5, this);
        table->setHorizontalHeaderLabels({"Rule", "Category", "Layer", "Count", "Severity"});
        table->setAlternatingRowColors(true);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table->verticalHeader()->setVisible(false);

        for (int i = 0; i < result.violations.size(); ++i) {
            const auto& v = result.violations[i];
            table->setItem(i, 0, new QTableWidgetItem(v.ruleName));
            table->setItem(i, 1, new QTableWidgetItem(v.category));
            table->setItem(i, 2, new QTableWidgetItem(v.layer));
            table->setItem(i, 3, new QTableWidgetItem(QString::number(v.count)));

            auto* sevItem = new QTableWidgetItem(v.severity.toUpper());
            if (v.severity == "error") {
                sevItem->setForeground(QColor("#f44747"));
            } else if (v.severity == "warning") {
                sevItem->setForeground(QColor("#dcdcaa"));
            } else {
                sevItem->setForeground(QColor("#569cd6"));
            }
            table->setItem(i, 4, sevItem);
        }

        m_tabs->addTab(table, QString("Violations (%1)").arg(result.violations.size()));
    }

    // --- Raw Log Tab ---
    auto* logEdit = new QTextEdit(this);
    logEdit->setReadOnly(true);
    logEdit->setPlainText(result.rawOutput);
    m_tabs->addTab(logEdit, "Raw Log");
}

// ============================================================================
// LVS CONSTRUCTOR
// ============================================================================

SignOffResultDialog::SignOffResultDialog(const LVSResult& result, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Sign-Off Results — LVS");
    setupCommonLayout();
    buildLVSView(result);
}

void SignOffResultDialog::buildLVSView(const LVSResult& result) {
    // --- Summary Tab ---
    auto* summaryWidget = new QWidget();
    auto* summaryLayout = new QVBoxLayout(summaryWidget);
    summaryLayout->setSpacing(12);

    // Match indicator
    auto* statusLabel = new QLabel(this);
    if (result.matched) {
        statusLabel->setText("✅  LVS MATCH — Layout matches schematic");
        statusLabel->setStyleSheet("color: #4ec9b0; font-size: 18px; font-weight: bold; padding: 12px;");
    } else {
        statusLabel->setText(QString("❌  LVS MISMATCH — %1 difference(s)")
            .arg(result.mismatchCount));
        statusLabel->setStyleSheet("color: #f44747; font-size: 18px; font-weight: bold; padding: 12px;");
    }
    summaryLayout->addWidget(statusLabel);

    // Device / Net counts
    auto* countsGroup = new QGroupBox("Extraction Summary", this);
    countsGroup->setStyleSheet("QGroupBox { color: #ccc; border: 1px solid #444; "
        "border-radius: 4px; margin-top: 8px; padding-top: 16px; }"
        "QGroupBox::title { subcontrol-origin: margin; padding: 0 6px; }");
    auto* countsForm = new QFormLayout(countsGroup);
    countsForm->setLabelAlignment(Qt::AlignRight);

    auto addCount = [&](const QString& label, int layout, int schematic) {
        QString text = QString("%1 (layout) / %2 (schematic)").arg(layout).arg(schematic);
        auto* v = new QLabel(text, this);
        bool match = (layout == schematic);
        v->setStyleSheet(match ? "color: #4ec9b0; font-weight: bold;"
                               : "color: #f44747; font-weight: bold;");
        countsForm->addRow(new QLabel(label, this), v);
    };
    addCount("Devices:", result.deviceCountLayout, result.deviceCountSchematic);
    addCount("Nets:", result.netCountLayout, result.netCountSchematic);

    summaryLayout->addWidget(countsGroup);
    summaryLayout->addStretch();

    m_tabs->addTab(summaryWidget, "Summary");

    // --- Mismatches Table Tab ---
    if (!result.mismatches.isEmpty()) {
        auto* table = new QTableWidget(result.mismatches.size(), 4, this);
        table->setHorizontalHeaderLabels({"Type", "Layout Name", "Schematic Name", "Details"});
        table->setAlternatingRowColors(true);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table->verticalHeader()->setVisible(false);

        for (int i = 0; i < result.mismatches.size(); ++i) {
            const auto& mm = result.mismatches[i];
            table->setItem(i, 0, new QTableWidgetItem(mm.type.toUpper()));
            table->setItem(i, 1, new QTableWidgetItem(mm.layoutName));
            table->setItem(i, 2, new QTableWidgetItem(mm.schematicName));
            table->setItem(i, 3, new QTableWidgetItem(mm.details));
        }

        m_tabs->addTab(table, QString("Mismatches (%1)").arg(result.mismatches.size()));
    }

    // --- Raw Log Tab ---
    auto* logEdit = new QTextEdit(this);
    logEdit->setReadOnly(true);
    logEdit->setPlainText(result.rawOutput);
    m_tabs->addTab(logEdit, "Raw Log");
}

} // namespace acutesim::gui::orchestration
