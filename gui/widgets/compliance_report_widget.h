#ifndef COMPLIANCE_REPORT_WIDGET_H
#define COMPLIANCE_REPORT_WIDGET_H

#include <QWidget>
#include <QTableWidget>
#include <QLabel>
#include "../../components/pdk_compliance.h"

class ComplianceReportWidget : public QWidget {
    Q_OBJECT
public:
    ComplianceReportWidget(const ComplianceReport& report, QWidget* parent = nullptr);

private:
    void setupUi(const ComplianceReport& report);
};

#endif // COMPLIANCE_REPORT_WIDGET_H
