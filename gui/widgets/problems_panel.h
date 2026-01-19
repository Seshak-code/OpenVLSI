#pragma once

#include <QWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include "../analysis/erc_engine.h"

/**
 * ProblemsPanel - VS Code-style Problems Window
 * 
 * Displays ERC violations with severity icons, click-to-navigate,
 * and summary counts.
 */
class ProblemsPanel : public QWidget {
    Q_OBJECT
public:
    explicit ProblemsPanel(QWidget* parent = nullptr);
    
    // Set violations from ERC engine
    void setViolations(const QList<ERC::Violation>& violations);
    
    // Clear all violations
    void clear();
    
    // Get count of blocking errors
    int getErrorCount() const { return errorCount_; }
    bool hasBlockingErrors() const { return errorCount_ > 0; }

signals:
    // Emitted when user double-clicks a violation to navigate to it
    void navigateToLocation(QPoint gridLocation);
    
    // Emitted when user wants to auto-fix a violation
    void requestAutoFix(int violationIndex);

private slots:
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onClearClicked();

private:
    QTreeWidget* treeWidget_;
    QLabel* summaryLabel_;
    QPushButton* clearBtn_;
    QPushButton* runErcBtn_;
    
    int errorCount_ = 0;
    int warningCount_ = 0;
    int infoCount_ = 0;
    
    QList<ERC::Violation> violations_;
    
    void updateSummary();
    QIcon getSeverityIcon(ERC::Severity severity) const;
    QColor getSeverityColor(ERC::Severity severity) const;
};
