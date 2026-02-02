#include "sim_auto_detector.h"
#include <QFileInfo>
#include <QRegularExpression>
#include <QDebug>

namespace acutesim::gui::core {

SimAutoDetector::SimAutoDetector(QObject* parent) : QObject(parent) {}

SimulationContext SimAutoDetector::detect(const QString& filePath, const QString& content) {
    SimulationContext ctx;
    ctx.mainSourceFile = filePath;
    
    // 1. Check User Override
    if (overrides_.contains(filePath)) {
        ctx.type = overrides_.value(filePath);
        ctx.isOverride = true;
        return ctx;
    }
    
    // 2. Check File Extension
    ctx.type = detectFromExtension(filePath);
    
    // 3. Fallback to Content Heuristics if ambiguous or generic
    if (ctx.type == SimulationType::None || ctx.type == SimulationType::Digital_Verilog) {
        // Only peek at content if we suspect mixed-signal or if extension is ambiguous
        if (!content.isEmpty()) {
            SimulationType contentType = detectFromContent(content);
            if (contentType != SimulationType::None) {
                ctx.type = contentType;
            }
        }
    }
    
    return ctx;
}

void SimAutoDetector::setOverride(const QString& filePath, SimulationType type) {
    overrides_.insert(filePath, type);
}

void SimAutoDetector::clearOverride(const QString& filePath) {
    overrides_.remove(filePath);
}

SimulationType SimAutoDetector::detectFromExtension(const QString& filePath) {
    QString ext = QFileInfo(filePath).suffix().toLower();
    
    if (ext == "sch") return SimulationType::Analog_SPICE;
    if (ext == "v" || ext == "sv") return SimulationType::Digital_Verilog;
    if (ext == "vams" || ext == "va") return SimulationType::Mixed_Signal;
    if (ext == "tcl" || ext == "py") return SimulationType::Script_Execution;
    
    return SimulationType::None;
}

SimulationType SimAutoDetector::detectFromContent(const QString& content) {
    // Heuristic: Check for Verilog-AMS keywords
    if (content.contains("electrical", Qt::CaseInsensitive) && 
        content.contains("module", Qt::CaseInsensitive)) {
        return SimulationType::Mixed_Signal;
    }
    
    // Heuristic: Check for SPICE netlist syntax
    if (content.startsWith("*") || content.contains(".subckt", Qt::CaseInsensitive)) {
        return SimulationType::Analog_SPICE;
    }
    
    return SimulationType::None;
}

QString SimAutoDetector::typeToString(SimulationType type) {
    switch (type) {
        case SimulationType::Analog_SPICE: return "Analog (SPICE)";
        case SimulationType::Digital_Verilog: return "Digital (Verilog)";
        case SimulationType::Mixed_Signal: return "Mixed-Signal";
        case SimulationType::Script_Execution: return "Script";
        default: return "None";
    }
}

} // namespace acutesim::gui::core
