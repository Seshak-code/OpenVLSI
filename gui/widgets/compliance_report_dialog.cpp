#include "compliance_report_dialog.h"
#include <QVBoxLayout>
#include <QHeaderView>
#include <QPushButton>

ComplianceReportDialog::ComplianceReportDialog(const ComplianceReport& report, QWidget* parent) 
    : QDialog(parent) {
    setWindowTitle("PDK Compliance Audit Report");
    resize(700, 450);
    setStyleSheet("background-color: #1E1E1E; color: #D4D4D4;");
    setupUi(report);
}

void ComplianceReportDialog::setupUi(const ComplianceReport& report) {
    QVBoxLayout* layout = new QVBoxLayout(this);

    QLabel* titleLabel = new QLabel("PDK Compliance Audit Result");
    titleLabel->setStyleSheet("font-size: 16pt; font-weight: bold; color: #569CD6; margin-bottom: 5px;");
    layout->addWidget(titleLabel);

    QLabel* summaryLabel = new QLabel(QString("Status: %1 | Errors: %2 | Warnings: %3")
        .arg(report.passed ? "PASSED" : "FAILED")
        .arg(report.errorCount)
        .arg(report.warningCount));
    
    QString summaryStyle = report.passed ? "color: #4EC9B0; font-weight: bold; font-size: 12pt; padding: 5px; border: 1px solid #4EC9B0; border-radius: 4px;" 
                                         : "color: #F44747; font-weight: bold; font-size: 12pt; padding: 5px; border: 1px solid #F44747; border-radius: 4px;";
    summaryLabel->setStyleSheet(summaryStyle);
    layout->addWidget(summaryLabel);

    QTableWidget* table = new QTableWidget(report.violations.size(), 4);
    table->setHorizontalHeaderLabels({"Component", "Type", "Severity", "Message"});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setStyleSheet("QTableWidget { background-color: #252526; alternate-background-color: #2D2D2D; border: 1px solid #333; }"
                         "QHeaderView::section { background-color: #333337; color: #BBB; padding: 4px; border: 1px solid #444; }");

    for (int i = 0; i < report.violations.size(); ++i) {
        const auto& v = report.violations[i];
        table->setItem(i, 0, new QTableWidgetItem(v.componentId));
        table->setItem(i, 1, new QTableWidgetItem(v.componentType));
        
        QString sevStr = (v.severity == ComplianceSeverity::ERROR) ? "ERROR" : "WARNING";
        QTableWidgetItem* sevItem = new QTableWidgetItem(sevStr);
        if (v.severity == ComplianceSeverity::ERROR) sevItem->setForeground(QColor("#F44747"));
        else sevItem->setForeground(QColor("#DCDCAA"));
        
        table->setItem(i, 2, sevItem);
        table->setItem(i, 3, new QTableWidgetItem(v.message));
    }
    layout->addWidget(table);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    QPushButton* closeBtn = new QPushButton("Close");
    closeBtn->setFixedWidth(100);
    closeBtn->setStyleSheet("QPushButton { background-color: #3C3C3C; border: 1px solid #555; padding: 5px; color: white; }"
                            "QPushButton:hover { background-color: #505050; }");
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(closeBtn);
    layout->addLayout(btnLayout);
}
