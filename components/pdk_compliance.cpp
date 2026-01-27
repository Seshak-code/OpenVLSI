#include "pdk_compliance.h"
#include "../gui/components/visual_component.h"
#include "../gui/editors/schematic_editor.h"
#include <QGraphicsItem>

const QSet<QString> PDKComplianceChecker::ALLOWED_MOSFET_MODELS = {
    "nfet_01v8", "nfet_01v8_lvt", "nfet_g5v0d10v5",
    "pfet_01v8", "pfet_01v8_lvt", "pfet_g5v0d10v5",
    "sky130_fd_pr__nfet_01v8", "sky130_fd_pr__pfet_01v8"
};

const QSet<QString> PDKComplianceChecker::ALLOWED_RESISTOR_MODELS = {
    "poly_res", "diffusion_res", "metal_res", "sky130_fd_pr__res_high_po"
};

const QSet<QString> PDKComplianceChecker::ALLOWED_CAPACITOR_MODELS = {
    "mim_cap", "varactor", "sky130_fd_pr__cap_mim_m3_1"
};

PDKComplianceChecker::PDKComplianceChecker() {}

ComplianceReport PDKComplianceChecker::checkCompliance(GridScene* scene) {
    ComplianceReport report;
    if (!scene) return report;

    for (auto* item : scene->items()) {
        auto* comp = dynamic_cast<VisualComponent*>(item);
        if (!comp) continue;
 
        // Skip wires and pins (they are handled separately or inherent)
        if (comp->type == ComponentType::Wire || comp->type == ComponentType::Pin) continue;

        QString model = comp->params.value("Model");
        
        // --- Rule 1: No Generic MOSFETs ---
        if (comp->type == ComponentType::PDK_NFET || comp->type == ComponentType::PDK_PFET || comp->type == ComponentType::Mosfet) {
            if (model.isEmpty()) {
                report.violations.append({comp->name, "MOSFET", "Missing Model property", ComplianceSeverity::ERROR});
                report.errorCount++;
            } else if (!ALLOWED_MOSFET_MODELS.contains(model)) {
                report.violations.append({comp->name, "MOSFET", QString("Model '%1' is not Sky130 compliant").arg(model), ComplianceSeverity::ERROR});
                report.errorCount++;
            }
        }

        // --- Rule 2: PDK Resistor/Capacitor Validation ---
        if (comp->type == ComponentType::PDK_Resistor || comp->type == ComponentType::Resistor) {
            if (model.isEmpty() || !ALLOWED_RESISTOR_MODELS.contains(model)) {
                report.violations.append({comp->name, "Resistor", "Using generic/ideal resistor in tapeout flow", ComplianceSeverity::WARNING});
                report.warningCount++;
            }
        }
        
        if (comp->type == ComponentType::PDK_Capacitor || comp->type == ComponentType::Capacitor) {
            if (model.isEmpty() || !ALLOWED_CAPACITOR_MODELS.contains(model)) {
                report.violations.append({comp->name, "Capacitor", "Using generic/ideal capacitor in tapeout flow", ComplianceSeverity::WARNING});
                report.warningCount++;
            }
        }

        // --- Rule 3: Forbidden Components ---
        if (comp->type == ComponentType::ICPackage) {
             report.violations.append({comp->name, "ICPackage", "Generic IC Packages are forbidden in monolithic PDK flow", ComplianceSeverity::ERROR});
             report.errorCount++;
        }
        
        // --- Rule 4: SubCircuit Validation ---
        // (For now, just a placeholder - nested blocks are audited when opened)
        if (comp->type == ComponentType::SubCircuit && comp->sourceFilePath.isEmpty()) {
             report.warningCount++;
             report.violations.append({comp->name, "SubCircuit", "Inline subcircuit has no source file (cannot audit internals)", ComplianceSeverity::WARNING});
        }
    }

    report.passed = (report.errorCount == 0);
    return report;
}
