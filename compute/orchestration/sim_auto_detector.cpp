#include "sim_auto_detector.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>

namespace acutesim::orchestration {

SimulationContext SimAutoDetector::detectFromActiveFile(const QString& filePath) {
    SimulationContext ctx;
    ctx.contextId = QFileInfo(filePath).fileName();
    
    FileRef mainRef;
    mainRef.path = filePath;
    mainRef.isTop = true;
    
    SimulationType type = guessTypeFromExtension(filePath);
    
    // Heuristic Check: Deep inspection for .v files that might be Verilog-A or structural
    if (type == SimulationType::Digital) {
        if (isAnalogContent(filePath)) {
            type = SimulationType::Analog; // It's Verilog-A or mapped structural
            // TODO: In future, this might be MixedSignal if it instantiates digital blocks
        }
    }
    
    ctx.type = type;
    mainRef.isSchematic = (type == SimulationType::Analog && filePath.endsWith(".sch"));
    
    ctx.sources.push_back(mainRef);
    ctx.entryPoint = filePath; // Default to file itself
    
    return ctx;
}

SimulationType SimAutoDetector::guessTypeFromExtension(const QString& path) {
    QString ext = QFileInfo(path).suffix().toLower();
    
    if (ext == "sch") return SimulationType::Analog;
    if (ext == "v" || ext == "sv") return SimulationType::Digital;
    if (ext == "va" || ext == "vams") return SimulationType::Analog;
    if (ext == "py" || ext == "tcl") return SimulationType::Script;
    
    return SimulationType::None;
}

bool SimAutoDetector::isAnalogContent(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    
    QTextStream in(&file);
    QString content = in.readAll();
    
    // Keywords for Verilog-A or Analog behavior
    if (content.contains("analog begin") || 
        content.contains("electrical ") || 
        content.contains("ground ")) {
        return true;
    }
    
    // Structural mapping hints? (e.g. specialized primitives)
    // For now, assume conservative (not analog unless explicit)
    return false;
}

} // namespace acutesim::orchestration
