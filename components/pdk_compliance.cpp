#include "pdk_compliance.h"
#include "../gui/components/visual_component.h"
#include "../gui/editors/schematic_editor.h"
#include "pdk_manager.h"
#include <QGraphicsItem>
#include <QDebug>

PDKComplianceChecker::PDKComplianceChecker() {}

ComplianceReport PDKComplianceChecker::checkCompliance(GridScene* scene) {
    ComplianceReport report;
    if (!scene) return report;

    // Get Active Rules
    const PDKRules& pdk = PDKManager::instance().getPDK();
    if (pdk.name.isEmpty()) {
        // Attempt load default if not loaded
        PDKManager::instance().loadPDK("resources/pdk/sky130.json");
    }
    const PDKRules& rules = PDKManager::instance().getPDK();

    for (auto* item : scene->items()) {
        auto* comp = dynamic_cast<VisualComponent*>(item);
        if (!comp) continue;
 
        // Skip wires and pins
        if (comp->type == ComponentType::Wire || comp->type == ComponentType::Pin) continue;
        if (comp->type == ComponentType::Ground || comp->type == ComponentType::PDK_VSS) continue;
        if (comp->type == ComponentType::PDK_VDD || comp->type == ComponentType::VoltageSource) continue;

        QString model = comp->params.value("Model");
        QString devClass = rules.getDeviceClass(model);
        
        // --- Rule 1: Unknown Model (if PDK defines classes) ---
        if (!rules.deviceClasses.isEmpty() && devClass == "UNKNOWN" && !model.isEmpty()) {
             // If model is set but not in PDK
             report.violations.append({comp->name, "Unknown", QString("Model '%1' not found in %2").arg(model).arg(rules.name), ComplianceSeverity::ERROR});
             report.errorCount++;
             continue; // Cannot check params for unknown model
        }
        
        // --- Rule 2: Validation by Class ---
        if (devClass == "MOSFET") {
             // Generic checks if needed
        }
        
        // --- Rule 3: Parametric Validation ---
        if (rules.paramRules.contains(model)) {
            const auto& pRules = rules.paramRules[model];
            for (auto it = pRules.begin(); it != pRules.end(); ++it) {
                QString paramName = it.key();
                const ParamRule& rule = it.value();
                
                if (comp->params.contains(paramName)) {
                    QVariant val = comp->params.value(paramName); // This is string usually?
                    // Convert string "1.0u" to double 1.0e-6 if needed
                    // For now assume simple numeric string "0.5" or handle unit suffix parsing later
                    
                    // Simple parse
                    bool ok;
                    double d = val.toString().toDouble(&ok);
                    if (ok) {
                        if (rule.min > -1e8 && d < rule.min) {
                            report.violations.append({
                                comp->name, devClass,
                                QString("Parameter '%1' (%2) < min (%3%4)").arg(paramName).arg(d).arg(rule.min).arg(rule.units),
                                ComplianceSeverity::ERROR
                            });
                            report.errorCount++;
                        }
                    }
                }
            }
        }
    }

    report.passed = (report.errorCount == 0);
    return report;
}
