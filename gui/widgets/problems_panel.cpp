#include "problems_panel.h"
#include <QHeaderView>
#include <QStyle>
#include <QApplication>
#include <QHBoxLayout>

ProblemsPanel::ProblemsPanel(QWidget* parent)
    : QWidget(parent)
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(4);
    
    // --- Header Row ---
    QHBoxLayout* headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(8, 4, 8, 4);
    
    summaryLabel_ = new QLabel("No problems detected");
    summaryLabel_->setStyleSheet("font-weight: bold; color: #888;");
    headerLayout->addWidget(summaryLabel_);
    
    headerLayout->addStretch();
    
    runErcBtn_ = new QPushButton("Run ERC");
    runErcBtn_->setMaximumWidth(80);
    runErcBtn_->setToolTip("Run Electrical Rule Check");
    headerLayout->addWidget(runErcBtn_);
    
    clearBtn_ = new QPushButton("Clear");
    clearBtn_->setMaximumWidth(60);
    connect(clearBtn_, &QPushButton::clicked, this, &ProblemsPanel::onClearClicked);
    headerLayout->addWidget(clearBtn_);
    
    mainLayout->addLayout(headerLayout);
    
    // --- Tree Widget ---
    treeWidget_ = new QTreeWidget(this);
    treeWidget_->setHeaderLabels({"", "Type", "Message", "Location"});
    treeWidget_->setColumnWidth(0, 30);  // Icon column
    treeWidget_->setColumnWidth(1, 120); // Type
    treeWidget_->setColumnWidth(2, 400); // Message
    treeWidget_->setColumnWidth(3, 80);  // Location
    
    treeWidget_->setRootIsDecorated(false);
    treeWidget_->setAlternatingRowColors(true);
    treeWidget_->setSelectionMode(QAbstractItemView::SingleSelection);
    treeWidget_->header()->setStretchLastSection(true);
    
    // Style
    treeWidget_->setStyleSheet(R"(
        QTreeWidget {
            border: none;
            background-color: #1e1e1e;
            color: #cccccc;
            font-family: 'Menlo', 'Consolas', monospace;
            font-size: 12px;
        }
        QTreeWidget::item {
            padding: 4px 2px;
            border-bottom: 1px solid #333;
        }
        QTreeWidget::item:selected {
            background-color: #264f78;
        }
        QTreeWidget::item:hover {
            background-color: #2a2d2e;
        }
        QHeaderView::section {
            background-color: #252526;
            color: #888;
            padding: 4px;
            border: none;
            border-right: 1px solid #333;
        }
    )");
    
    connect(treeWidget_, &QTreeWidget::itemDoubleClicked, 
            this, &ProblemsPanel::onItemDoubleClicked);
    
    mainLayout->addWidget(treeWidget_);
    
    setLayout(mainLayout);
}

void ProblemsPanel::setViolations(const QList<ERC::Violation>& violations) {
    violations_ = violations;
    treeWidget_->clear();
    
    errorCount_ = 0;
    warningCount_ = 0;
    infoCount_ = 0;
    
    for (int i = 0; i < violations.size(); ++i) {
        const ERC::Violation& v = violations[i];
        
        QTreeWidgetItem* item = new QTreeWidgetItem();
        
        // Column 0: Severity Icon
        item->setIcon(0, getSeverityIcon(v.severity));
        
        // Column 1: Type
        item->setText(1, ERC::typeToString(v.type));
        
        // Column 2: Message
        item->setText(2, v.message);
        
        // Column 3: Location
        if (v.location.x() != -9999 && v.location.y() != -9999) {
            item->setText(3, QString("(%1, %2)").arg(v.location.x()).arg(v.location.y()));
        } else {
            item->setText(3, "Global");
        }
        
        // Store index for navigation
        item->setData(0, Qt::UserRole, i);
        
        // Row coloring based on severity
        QColor rowColor = getSeverityColor(v.severity);
        for (int col = 0; col < 4; ++col) {
            item->setForeground(col, QBrush(rowColor));
        }
        
        treeWidget_->addTopLevelItem(item);
        
        // Count by severity
        switch (v.severity) {
            case ERC::Severity::ERROR:   errorCount_++;   break;
            case ERC::Severity::WARNING: warningCount_++; break;
            case ERC::Severity::INFO:    infoCount_++;    break;
        }
    }
    
    updateSummary();
}

void ProblemsPanel::clear() {
    violations_.clear();
    treeWidget_->clear();
    errorCount_ = warningCount_ = infoCount_ = 0;
    updateSummary();
}

void ProblemsPanel::updateSummary() {
    if (violations_.isEmpty()) {
        summaryLabel_->setText("No problems detected");
        summaryLabel_->setStyleSheet("font-weight: bold; color: #4ec9b0;"); // Green
    } else {
        QString summary;
        if (errorCount_ > 0) {
            summary += QString("%1 Error%2").arg(errorCount_).arg(errorCount_ > 1 ? "s" : "");
        }
        if (warningCount_ > 0) {
            if (!summary.isEmpty()) summary += ", ";
            summary += QString("%1 Warning%2").arg(warningCount_).arg(warningCount_ > 1 ? "s" : "");
        }
        if (infoCount_ > 0) {
            if (!summary.isEmpty()) summary += ", ";
            summary += QString("%1 Info").arg(infoCount_);
        }
        summaryLabel_->setText(summary);
        
        if (errorCount_ > 0) {
            summaryLabel_->setStyleSheet("font-weight: bold; color: #f44747;"); // Red
        } else if (warningCount_ > 0) {
            summaryLabel_->setStyleSheet("font-weight: bold; color: #cca700;"); // Yellow
        } else {
            summaryLabel_->setStyleSheet("font-weight: bold; color: #3794ff;"); // Blue
        }
    }
}

void ProblemsPanel::onItemDoubleClicked(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column);
    if (!item) return;
    
    int idx = item->data(0, Qt::UserRole).toInt();
    if (idx >= 0 && idx < violations_.size()) {
        const ERC::Violation& v = violations_[idx];
        
        // Skip global violations (can't navigate)
        if (v.location.x() != -9999 && v.location.y() != -9999) {
            emit navigateToLocation(v.location);
        }
    }
}

void ProblemsPanel::onClearClicked() {
    clear();
}

QIcon ProblemsPanel::getSeverityIcon(ERC::Severity severity) const {
    // Use Qt's standard icons as fallback, or custom icons if available
    switch (severity) {
        case ERC::Severity::ERROR:
            return style()->standardIcon(QStyle::SP_MessageBoxCritical);
        case ERC::Severity::WARNING:
            return style()->standardIcon(QStyle::SP_MessageBoxWarning);
        case ERC::Severity::INFO:
            return style()->standardIcon(QStyle::SP_MessageBoxInformation);
        default:
            return QIcon();
    }
}

QColor ProblemsPanel::getSeverityColor(ERC::Severity severity) const {
    switch (severity) {
        case ERC::Severity::ERROR:   return QColor("#f44747"); // VS Code red
        case ERC::Severity::WARNING: return QColor("#cca700"); // VS Code yellow
        case ERC::Severity::INFO:    return QColor("#3794ff"); // VS Code blue
        default:                     return QColor("#cccccc");
    }
}
