#pragma once

#include <QString>
#include <QList>

namespace Simulation {

/**
 * Corner configuration for PVT (Process/Voltage/Temperature) analysis.
 * Represents a single operating condition for simulation.
 */
struct CornerConfig {
    QString name;           // "TT", "FF", "SS", "SF", "FS"
    double temperature;     // Temperature in °C (e.g., -40, 27, 125)
    double vddScale;        // VDD scaling factor (e.g., 0.9, 1.0, 1.1)
    QString processFile;    // Optional: path to process model file
    
    // Predefined corners
    static CornerConfig typical() {
        return {"TT", 27.0, 1.0, ""};
    }
    
    static CornerConfig fast() {
        return {"FF", -40.0, 1.1, ""};
    }
    
    static CornerConfig slow() {
        return {"SS", 125.0, 0.9, ""};
    }
    
    static CornerConfig slowFast() {
        return {"SF", 27.0, 1.0, ""};
    }
    
    static CornerConfig fastSlow() {
        return {"FS", 27.0, 1.0, ""};
    }
    
    // Standard 5-corner set
    static QList<CornerConfig> standard5() {
        return {typical(), fast(), slow(), slowFast(), fastSlow()};
    }
    
    // Temperature corners (typical process)
    static QList<CornerConfig> temperatureSweep() {
        return {
            {"TT_cold", -40.0, 1.0, ""},
            {"TT_nom",   27.0, 1.0, ""},
            {"TT_hot",  125.0, 1.0, ""}
        };
    }
    
    // Voltage corners (typical process, room temp)
    static QList<CornerConfig> voltageSweep() {
        return {
            {"TT_low",  27.0, 0.9, ""},
            {"TT_nom",  27.0, 1.0, ""},
            {"TT_high", 27.0, 1.1, ""}
        };
    }
};

/**
 * Run mode for simulation execution.
 */
enum class RunMode {
    FULL,        // Complete simulation from t=0
    QUICK,       // Single Newton iteration (for quick preview)
    RESUME,      // Continue from last state
    INCREMENTAL  // Re-simulate only changed subcircuits
};

inline QString runModeToString(RunMode mode) {
    switch (mode) {
        case RunMode::FULL: return "Full";
        case RunMode::QUICK: return "Quick";
        case RunMode::RESUME: return "Resume";
        case RunMode::INCREMENTAL: return "Incremental";
    }
    return "Full";
}

} // namespace Simulation
