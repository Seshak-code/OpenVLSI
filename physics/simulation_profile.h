#pragma once

#include <QString>
#include <QVariantMap>
#include <QStringList>
#include <QDateTime>
#include "../gui/core/gui_types.h"  // For AppMode enum

/**
 * simulation_profile.h
 * 
 * Defines simulation configuration profiles and run objects.
 * Part of Phase 1.7: VS Code-style Simulation Suite.
 */

namespace Simulation {

// ============================================================================
// ANALYSIS TYPES
// ============================================================================

enum class AnalysisType {
    DC,             // DC Operating Point
    AC,             // AC Small-Signal
    TRANSIENT,      // Time-domain transient
    NOISE,          // Noise analysis
    SENSITIVITY,    // Parameter sensitivity
    DCMATCH,        // DC matching/mismatch
    PZ              // Pole-Zero analysis
};

inline QString analysisToString(AnalysisType t) {
    switch (t) {
        case AnalysisType::DC: return "DC";
        case AnalysisType::AC: return "AC";
        case AnalysisType::TRANSIENT: return "Transient";
        case AnalysisType::NOISE: return "Noise";
        case AnalysisType::SENSITIVITY: return "Sensitivity";
        case AnalysisType::DCMATCH: return "DC Match";
        case AnalysisType::PZ: return "Pole-Zero";
        default: return "Unknown";
    }
}

// ============================================================================
// SIMULATION PROFILE
// ============================================================================

struct SimulationProfile {
    QString profileId;           // Unique ID: "ac_gain_nominal"
    QString name;                // Display name: "AC Gain Analysis"
    AnalysisType analysis = AnalysisType::DC;
    AppMode mode = AppMode::BENCH_TEST;
    
    // Topology binding
    size_t topologyHash = 0;     // Links to TensorBlock::topologyHash
    
    // Analysis-specific parameters
    QVariantMap parameters;      // {"fStart": "1Hz", "fStop": "1GHz", "points": 200}
    
    // Output definitions
    QStringList outputs;         // ["Vout/Vin", "Phase", "Vout"]
    
    // Prerequisites
    bool requiresDC = false;     // AC/Noise require DC first
    bool requiresERC = true;     // Block simulation if ERC fails
    
    // Profile status
    enum class Status { 
        READY,      // Ready to run
        STALE,      // Topology changed since last run
        RUNNING,    // Currently executing
        PASSED,     // Last run succeeded
        FAILED      // Last run failed
    };
    Status status = Status::READY;
    
    // Category for tree organization
    enum class Category { QUICK, ANALYSIS, SWEEP, CORNER };
    Category category = Category::ANALYSIS;
    
    // Default profiles factory
    static SimulationProfile quickDC() {
        SimulationProfile p;
        p.profileId = "quick_dc";
        p.name = "Quick DC";
        p.analysis = AnalysisType::DC;
        p.category = Category::QUICK;
        p.parameters["accuracy"] = "liberal";
        p.parameters["timeout_ms"] = 100;
        return p;
    }
    
    static SimulationProfile transient() {
        SimulationProfile p;
        p.profileId = "transient_default";
        p.name = "Transient";
        p.analysis = AnalysisType::TRANSIENT;
        p.category = Category::ANALYSIS;
        p.parameters["stopTime"] = "1ms";
        p.parameters["stepHint"] = "100ns";
        p.parameters["accuracy"] = "moderate";
        return p;
    }
    
    static SimulationProfile acSweep() {
        SimulationProfile p;
        p.profileId = "ac_sweep_default";
        p.name = "AC Sweep";
        p.analysis = AnalysisType::AC;
        p.category = Category::ANALYSIS;
        p.requiresDC = true;
        p.parameters["fStart"] = "1Hz";
        p.parameters["fStop"] = "1GHz";
        p.parameters["points"] = 200;
        p.parameters["sweepType"] = "log";
        return p;
    }
};

// ============================================================================
// SIMULATION RUN (First-Class Object)
// ============================================================================

struct SimulationRun {
    QString runId;               // UUID
    QString profileId;           // Which profile was used
    size_t topologyHash;         // Topology at time of run
    QDateTime startTime;
    QDateTime endTime;
    
    enum class Status { 
        PENDING, 
        RUNNING, 
        CONVERGED, 
        FAILED, 
        CANCELLED 
    };
    Status status = Status::PENDING;
    
    // Execution mode
    enum class Mode {
        FULL,           // Complete solve
        QUICK,          // Single iteration estimate
        RESUME,         // Continue from previous
        INCREMENTAL     // Reuse Jacobian if safe
    };
    Mode runMode = Mode::FULL;
    
    // Timing metrics
    double setupTime_ms = 0.0;
    double solveTime_ms = 0.0;
    int iterations = 0;
    
    // Result reference (index into trace)
    int resultIndex = -1;
    
    // Failure info
    QString failureReason;
    int failureNode = -1;        // Node where issue occurred
    
    // For AI context
    QString toSummary() const {
        QString s = QString("Run %1: %2")
            .arg(runId.left(8))
            .arg(status == Status::CONVERGED ? "PASS" : 
                 status == Status::FAILED ? "FAIL" : "...");
        if (status == Status::CONVERGED) {
            s += QString(" (%1 iterations, %2ms)")
                .arg(iterations)
                .arg(solveTime_ms, 0, 'f', 1);
        }
        if (status == Status::FAILED && !failureReason.isEmpty()) {
            s += QString(" - %1").arg(failureReason);
        }
        return s;
    }
};

// ============================================================================
// CORNER CONFIG (Foundation for 1.8)
// ============================================================================

struct CornerConfig {
    QString name;                // "TT @ 27C"
    double temperature_C = 27.0;
    double vdd_V = 1.8;
    QString process = "TT";      // TT, FF, SS, SF, FS
    
    static CornerConfig typical() {
        return {"TT @ 27C", 27.0, 1.8, "TT"};
    }
    
    static CornerConfig fast() {
        return {"FF @ -40C", -40.0, 1.98, "FF"};
    }
    
    static CornerConfig slow() {
        return {"SS @ 125C", 125.0, 1.62, "SS"};
    }
};

} // namespace Simulation
