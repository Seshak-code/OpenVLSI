#pragma once
#include "../../engine_api/simulation_context.h"
#include <string>
#include <vector>

namespace acutesim::orchestration {

class SimAutoDetector {
public:
    // Detect context from a single file (simple case)
    static SimulationContext detectFromActiveFile(const std::string& filePath);

    // Detect context from a set of files (project level)
    static SimulationContext detectFromProject(const std::vector<std::string>& files,
                                               const std::string& explicitTop = "");

    // Heuristics
    static SimulationType guessTypeFromExtension(const std::string& path);
    static bool isAnalogContent(const std::string& path); // Deep inspection
};

} // namespace acutesim::orchestration
