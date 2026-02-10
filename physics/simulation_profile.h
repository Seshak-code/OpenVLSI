#pragma once

#include <QString>
#include <QVariantMap>
#include <QStringList>
#include <QDateTime>
// #include "../gui/core/gui_types.h"  // AppMode Deprecated
#include "corner_config.h"          // For CornerConfig and RunMode

// Forward declaration - full definition in simulation_intent.h
namespace Simulation { struct TrustSignature; }

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
    MONTE_CARLO,    // Statistical distribution (Phase 1.1)
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
    // AppMode deprecated in favor of unified GUI
    // AppMode mode = AppMode::BENCH_TEST;
    
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

    static SimulationProfile noiseSweep() {
        SimulationProfile p;
        p.profileId = "noise_sweep_default";
        p.name = "Noise Sweep";
        p.analysis = AnalysisType::NOISE;
        p.category = Category::ANALYSIS;
        p.requiresDC = true;
        p.parameters["fStart"] = "1Hz";
        p.parameters["fStop"] = "100MHz";
        p.parameters["points"] = 200;
        p.parameters["outputNode"] = 1;
        p.parameters["inputNode"] = 0;
        p.parameters["temperature_K"] = 300.15;
        return p;
    }
    
    // Corner configurations for PVT analysis
    QList<acutesim::physics::CornerConfig> corners;  // Default: empty = typical corner only
    
    // Run mode preference
    acutesim::physics::RunMode runMode = acutesim::physics::RunMode::FULL;
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
    
    // Execution mode from corner_config.h
    acutesim::physics::RunMode runMode = acutesim::physics::RunMode::FULL;
    
    // Timing metrics
    double setupTime_ms = 0.0;
    double solveTime_ms = 0.0;
    int iterations = 0;
    
    // Result reference (index into trace)
    int resultIndex = -1;
    
    // Failure info
    QString failureReason;
    int failureNode = -1;        // Node where issue occurred
    
    // Phase 1.7.9: Trust & Intent
    // Note: TrustSignature is forward-declared; include simulation_intent.h for full struct
    QString trustMethod;              // "Newton-Raphson", "GMIN Stepping"
    QString trustIntegration;         // "Trapezoidal", "Gear2"
    double trustMaxResidual = 0.0;    // Max KCL error (Amps)
    bool trustGminUsed = false;
    bool trustSourceStepUsed = false;
    QString trustProvenance;          // Full explanation
    QStringList linkedIntentIds;      // Intents evaluated by this run
    
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

// CornerConfig is now in corner_config.h

} // namespace Simulation
