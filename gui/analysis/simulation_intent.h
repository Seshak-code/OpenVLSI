#pragma once

#include <QString>
#include <QVariantMap>
#include <QList>
#include "corner_config.h"
#include "simulation_profile.h"

/**
 * simulation_intent.h
 *
 * Phase 1.7.9: Simulation Intent & Trust Surface
 *
 * This file defines:
 * - SimulationIntent: Engineering objectives with pass/fail criteria.
 * - TrustSignature: Solver evidence showing WHY a result is trusted.
 */

namespace Simulation {

// ============================================================================
// METRIC TYPES
// ============================================================================

enum class MetricType {
    GAIN_DB,        // 20*log10(Vout/Vin)
    PHASE_DEG,      // Phase difference in degrees
    VOLTAGE,        // Absolute voltage at a node
    CURRENT,        // Branch current
    DELAY,          // Propagation delay (rise/fall)
    SLEW,           // Slew rate (V/s)
    POWER,          // Power dissipation
    CUSTOM          // User-defined expression
};

inline QString metricTypeToString(MetricType t) {
    switch (t) {
        case MetricType::GAIN_DB: return "Gain (dB)";
        case MetricType::PHASE_DEG: return "Phase (deg)";
        case MetricType::VOLTAGE: return "Voltage";
        case MetricType::CURRENT: return "Current";
        case MetricType::DELAY: return "Delay";
        case MetricType::SLEW: return "Slew Rate";
        case MetricType::POWER: return "Power";
        case MetricType::CUSTOM: return "Custom";
        default: return "Unknown";
    }
}

// ============================================================================
// INTENT METRIC
// ============================================================================

struct IntentMetric {
    MetricType type = MetricType::VOLTAGE;
    QString expression;         // e.g., "Vout/Vin", "V(probe1)", "I(R1)"
    double lowerBound = 0.0;    // Minimum acceptable value
    double upperBound = 1e9;    // Maximum acceptable value (large = no limit)
    QString unit;               // Display unit (dB, V, A, etc.)

    bool isWithinBounds(double value) const {
        return value >= lowerBound && value <= upperBound;
    }

    QString boundsToString() const {
        if (lowerBound > -1e8 && upperBound < 1e8) {
            return QString("%1 ≤ %2 ≤ %3 %4")
                .arg(lowerBound, 0, 'g', 4)
                .arg(expression)
                .arg(upperBound, 0, 'g', 4)
                .arg(unit);
        } else if (lowerBound > -1e8) {
            return QString("%1 ≥ %2 %3").arg(expression).arg(lowerBound, 0, 'g', 4).arg(unit);
        } else if (upperBound < 1e8) {
            return QString("%1 ≤ %2 %3").arg(expression).arg(upperBound, 0, 'g', 4).arg(unit);
        }
        return expression;
    }
};

// ============================================================================
// INTENT CONDITION
// ============================================================================

struct IntentCondition {
    AnalysisType analysis = AnalysisType::DC;
    QVariantMap parameters;     // e.g., {"frequency": 1000, "temperature": 27}
    CornerConfig corner;        // PVT corner for this check

    QString toSummary() const {
        QString s = analysisToString(analysis);
        if (parameters.contains("frequency")) {
            s += QString(" @ %1 Hz").arg(parameters["frequency"].toDouble());
        }
        if (!corner.name.isEmpty()) {
            s += QString(" [%1]").arg(corner.name);
        }
        return s;
    }
};

// ============================================================================
// VERDICT
// ============================================================================

enum class VerdictStatus {
    UNTESTED,   // Not yet evaluated
    PASS,       // Metric within bounds
    FAIL,       // Metric out of bounds
    MARGINAL,   // Within 10% of boundary
    ERR       // Could not evaluate (sim failed, expression invalid)
};

inline QString verdictToString(VerdictStatus v) {
    switch (v) {
        case VerdictStatus::UNTESTED: return "UNTESTED";
        case VerdictStatus::PASS: return "PASS";
        case VerdictStatus::FAIL: return "FAIL";
        case VerdictStatus::MARGINAL: return "MARGINAL";
        case VerdictStatus::ERR: return "ERROR";
        default: return "?";
    }
}

struct IntentVerdict {
    VerdictStatus status = VerdictStatus::UNTESTED;
    double measuredValue = 0.0;
    QString message;            // e.g., "Gain = 62.3 dB (> 60 dB threshold)"

    static IntentVerdict fromCheck(const IntentMetric& metric, double value) {
        IntentVerdict v;
        v.measuredValue = value;
        if (metric.isWithinBounds(value)) {
            // Check for marginal (within 10% of boundary)
            double range = metric.upperBound - metric.lowerBound;
            double marginLow = metric.lowerBound + 0.1 * range;
            double marginHigh = metric.upperBound - 0.1 * range;
            if (value < marginLow || value > marginHigh) {
                v.status = VerdictStatus::MARGINAL;
                v.message = QString("%1 = %2 %3 (marginal)")
                    .arg(metric.expression)
                    .arg(value, 0, 'g', 4)
                    .arg(metric.unit);
            } else {
                v.status = VerdictStatus::PASS;
                v.message = QString("%1 = %2 %3 (PASS)")
                    .arg(metric.expression)
                    .arg(value, 0, 'g', 4)
                    .arg(metric.unit);
            }
        } else {
            v.status = VerdictStatus::FAIL;
            v.message = QString("%1 = %2 %3 (FAIL: out of [%4, %5])")
                .arg(metric.expression)
                .arg(value, 0, 'g', 4)
                .arg(metric.unit)
                .arg(metric.lowerBound, 0, 'g', 4)
                .arg(metric.upperBound, 0, 'g', 4);
        }
        return v;
    }
};

// ============================================================================
// TRUST SIGNATURE
// ============================================================================

/**
 * TrustSignature captures the "why" of a simulation result.
 * This is the evidence that allows a user (or auditor) to trust the numbers.
 */
struct TrustSignature {
    // Solver method used
    QString method = "Newton-Raphson";  // or "Source Stepping", "GMIN Stepping"

    // Integration method (for transient)
    QString integrationMethod;          // "Trapezoidal", "Gear2", "Backward Euler"

    // Convergence metrics
    int iterations = 0;
    double maxResidual = 0.0;           // Max KCL error (Amps)
    double convergenceTolerance = 1e-6; // Tolerance used

    // Recovery techniques applied
    bool gminSteppingUsed = false;
    bool sourceSteppingUsed = false;

    // Timing
    double solveTimeMs = 0.0;

    // Human-readable provenance
    QString provenance;                 // Full explanation

    QString toSummary() const {
        QStringList parts;
        parts << QString("Method: %1").arg(method);
        if (!integrationMethod.isEmpty()) {
            parts << QString("Integration: %1").arg(integrationMethod);
        }
        parts << QString("Iterations: %1").arg(iterations);
        parts << QString("Residual: %1 A").arg(maxResidual, 0, 'e', 2);
        if (gminSteppingUsed) parts << "GMIN stepping applied";
        if (sourceSteppingUsed) parts << "Source stepping applied";
        parts << QString("Solve time: %1 ms").arg(solveTimeMs, 0, 'f', 1);
        return parts.join(" | ");
    }

    void generateProvenance() {
        QStringList reasons;
        reasons << QString("Converged in %1 iterations using %2")
                      .arg(iterations).arg(method);
        if (!integrationMethod.isEmpty()) {
            reasons << QString("Integration: %1").arg(integrationMethod);
        }
        reasons << QString("Maximum KCL residual: %1 A (tolerance: %2 A)")
                      .arg(maxResidual, 0, 'e', 2)
                      .arg(convergenceTolerance, 0, 'e', 2);
        if (gminSteppingUsed) {
            reasons << "GMIN stepping was required for convergence";
        }
        if (sourceSteppingUsed) {
            reasons << "Source stepping was required for initial guess";
        }
        provenance = reasons.join(". ") + ".";
    }
};

// ============================================================================
// SIMULATION INTENT
// ============================================================================

/**
 * SimulationIntent defines an engineering objective.
 * Example: "Gain > 60dB at 1kHz for TT corner"
 */
struct SimulationIntent {
    QString intentId;           // UUID
    QString description;        // Human-readable: "Gain > 60dB"

    IntentMetric metric;
    IntentCondition condition;
    IntentVerdict verdict;
    TrustSignature trust;       // Populated after evaluation

    // Linked profile (which profile should run to evaluate this intent)
    QString linkedProfileId;

    QString toSummary() const {
        return QString("%1: %2 [%3]")
            .arg(description)
            .arg(verdictToString(verdict.status))
            .arg(condition.toSummary());
    }

    // Factory for common intents
    static SimulationIntent gainThreshold(const QString& expr, double minGainDb) {
        SimulationIntent intent;
        intent.intentId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        intent.description = QString("Gain ≥ %1 dB").arg(minGainDb);
        intent.metric.type = MetricType::GAIN_DB;
        intent.metric.expression = expr;
        intent.metric.lowerBound = minGainDb;
        intent.metric.upperBound = 200.0;  // Practical upper limit
        intent.metric.unit = "dB";
        intent.condition.analysis = AnalysisType::AC;
        return intent;
    }

    static SimulationIntent voltageRange(const QString& node, double vMin, double vMax) {
        SimulationIntent intent;
        intent.intentId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        intent.description = QString("%1 ≤ V(%2) ≤ %3")
            .arg(vMin, 0, 'g', 3).arg(node).arg(vMax, 0, 'g', 3);
        intent.metric.type = MetricType::VOLTAGE;
        intent.metric.expression = QString("V(%1)").arg(node);
        intent.metric.lowerBound = vMin;
        intent.metric.upperBound = vMax;
        intent.metric.unit = "V";
        intent.condition.analysis = AnalysisType::DC;
        return intent;
    }
};

} // namespace Simulation
