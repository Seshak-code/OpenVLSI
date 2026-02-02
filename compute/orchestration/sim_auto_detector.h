#pragma once
#include "simulation_context.h"
#include <QList>
#include <QFileInfo>

namespace acutesim::orchestration {

class SimAutoDetector {
public:
    // Detect context from a single file (Simple case)
    static SimulationContext detectFromActiveFile(const QString& filePath);

    // Detect context from a set of files (Project level)
    static SimulationContext detectFromProject(const QList<QString>& files, const QString& explicitTop = "");

    // Heuristics
    static SimulationType guessTypeFromExtension(const QString& path);
    static bool isAnalogContent(const QString& path); // Deep inspection
};

} // namespace acutesim::orchestration
