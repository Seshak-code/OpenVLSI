#ifndef PDK_COMPLIANCE_H
#define PDK_COMPLIANCE_H

#include <QString>
#include <QList>
#include <QSet>
#include <string>
#include <vector>

class GridScene;

enum class ComplianceSeverity {
    INFO,
    WARNING,
    ERROR
};

struct ComplianceViolation {
    QString componentId;
    QString componentType;
    QString message;
    ComplianceSeverity severity;
};

struct ComplianceReport {
    bool passed;
    QList<ComplianceViolation> violations;
    int errorCount;
    int warningCount;

    ComplianceReport() : passed(true), errorCount(0), warningCount(0) {}
};

class PDKComplianceChecker {
public:
    PDKComplianceChecker();

    // The main audit function
    static ComplianceReport checkCompliance(GridScene* scene);

private:
    static const QSet<QString> ALLOWED_MOSFET_MODELS;
    static const QSet<QString> ALLOWED_RESISTOR_MODELS;
    static const QSet<QString> ALLOWED_CAPACITOR_MODELS;
};

#endif // PDK_COMPLIANCE_H
