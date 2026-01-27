#ifndef COMPLIANCE_REPORT_DIALOG_H
#define COMPLIANCE_REPORT_DIALOG_H

#include <QDialog>
#include <QTableWidget>
#include <QLabel>
#include "../../components/pdk_compliance.h"

class ComplianceReportDialog : public QDialog {
    Q_OBJECT
public:
    ComplianceReportDialog(const ComplianceReport& report, QWidget* parent = nullptr);

private:
    void setupUi(const ComplianceReport& report);
};

#endif // COMPLIANCE_REPORT_DIALOG_H
