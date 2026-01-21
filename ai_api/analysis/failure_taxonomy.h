#pragma once
#include <QString>
#include <QSet>
#include <QJsonObject>
#include <QJsonArray>
#include <QList>
#include <QDateTime>

namespace AIAPI {

// ============================================================================
// FAILURE TAXONOMY - Machine-readable failure classification
// ============================================================================

/**
 * Enumeration of simulation failure types.
 * Used for structured AI context and RAG indexing.
 */
enum class FailureType {
    NONE = 0,               // No failure - simulation successful
    
    // Convergence Failures
    FLOATING_NODE,          // Node not connected to reference (ground)
    SINGULAR_JACOBIAN,      // Matrix is singular - structural issue
    CONVERGENCE_FAILED,     // Newton-Raphson max iterations exceeded
    
    // Fallback Mechanisms (Warnings, not hard failures)
    GMIN_FALLBACK,          // GMIN stepping was required for convergence
    SOURCE_STEPPING,        // Source ramping was required
    VOLTAGE_LIMITING,       // pnjlim or similar limiting triggered
    
    // Transient Issues
    LTE_VIOLATION,          // Local truncation error exceeded threshold
    TIMESTEP_TOO_SMALL,     // Adaptive dt reached minimum
    OVERFLOW_DETECTED,      // NaN/Inf in matrix operations
    
    // Topology Issues
    VOLTAGE_SOURCE_LOOP,    // Zero-impedance loop of voltage sources
    CURRENT_SOURCE_CUTSET,  // Current source(s) not in closed loop
    DISCONNECTED_COMPONENT, // Component has no path to ground
    NO_DC_PATH              // No DC path to ground
};

/**
 * Human-readable labels for failure types.
 */
inline QString failureTypeToString(FailureType type) {
    switch (type) {
        case FailureType::NONE:                 return "No Failure";
        case FailureType::FLOATING_NODE:        return "Floating Node";
        case FailureType::SINGULAR_JACOBIAN:    return "Singular Jacobian";
        case FailureType::CONVERGENCE_FAILED:   return "Convergence Failed";
        case FailureType::GMIN_FALLBACK:        return "GMIN Stepping Used";
        case FailureType::SOURCE_STEPPING:      return "Source Stepping Used";
        case FailureType::VOLTAGE_LIMITING:     return "Voltage Limiting Active";
        case FailureType::LTE_VIOLATION:        return "LTE Violation";
        case FailureType::TIMESTEP_TOO_SMALL:   return "Timestep Too Small";
        case FailureType::OVERFLOW_DETECTED:    return "Numerical Overflow";
        case FailureType::VOLTAGE_SOURCE_LOOP:  return "Voltage Source Loop";
        case FailureType::CURRENT_SOURCE_CUTSET:return "Current Source Cutset";
        case FailureType::DISCONNECTED_COMPONENT:return "Disconnected Component";
        case FailureType::NO_DC_PATH:           return "No DC Path to Ground";
        default:                                return "Unknown";
    }
}

/**
 * Severity level for failures.
 */
enum class FailureSeverity {
    INFO,       // Informational (e.g., fallback used but succeeded)
    WARNING,    // May affect accuracy
    ERROR,      // Simulation failed
    CRITICAL    // Data corruption / crash risk
};

inline FailureSeverity getFailureSeverity(FailureType type) {
    switch (type) {
        case FailureType::NONE:
            return FailureSeverity::INFO;
        case FailureType::GMIN_FALLBACK:
        case FailureType::SOURCE_STEPPING:
        case FailureType::VOLTAGE_LIMITING:
            return FailureSeverity::WARNING;
        case FailureType::FLOATING_NODE:
        case FailureType::CONVERGENCE_FAILED:
        case FailureType::LTE_VIOLATION:
        case FailureType::TIMESTEP_TOO_SMALL:
        case FailureType::NO_DC_PATH:
            return FailureSeverity::ERROR;
        case FailureType::SINGULAR_JACOBIAN:
        case FailureType::OVERFLOW_DETECTED:
        case FailureType::VOLTAGE_SOURCE_LOOP:
        case FailureType::CURRENT_SOURCE_CUTSET:
        case FailureType::DISCONNECTED_COMPONENT:
            return FailureSeverity::CRITICAL;
        default:
            return FailureSeverity::ERROR;
    }
}

/**
 * FailureLabel - A tagged failure instance with context.
 */
struct FailureLabel {
    FailureType type = FailureType::NONE;
    FailureSeverity severity = FailureSeverity::INFO;
    QString nodeOrComponent;   // e.g., "Node 5" or "Q1"
    QString message;           // Human-readable explanation
    int iteration = -1;        // Newton iteration when detected (-1 = N/A)
    double residual = 0.0;     // KCL residual at failure point
    
    QJsonObject toJson() const {
        QJsonObject obj;
        obj["type"] = static_cast<int>(type);
        obj["typeString"] = failureTypeToString(type);
        obj["severity"] = static_cast<int>(severity);
        obj["nodeOrComponent"] = nodeOrComponent;
        obj["message"] = message;
        obj["iteration"] = iteration;
        obj["residual"] = residual;
        return obj;
    }
    
    static FailureLabel fromJson(const QJsonObject& obj) {
        FailureLabel label;
        label.type = static_cast<FailureType>(obj["type"].toInt());
        label.severity = static_cast<FailureSeverity>(obj["severity"].toInt());
        label.nodeOrComponent = obj["nodeOrComponent"].toString();
        label.message = obj["message"].toString();
        label.iteration = obj["iteration"].toInt(-1);
        label.residual = obj["residual"].toDouble(0.0);
        return label;
    }
};

/**
 * FailureLabeler - Heuristic-based failure detection and labeling.
 */
class FailureLabeler {
public:
    /**
     * Analyze solver stats and netlist to detect failure patterns.
     */
    static QList<FailureLabel> labelFailures(const QJsonObject& solverStats,
                                              const QJsonObject& netlistState);
    
    /**
     * Convert a list of labels to a JSON array for AI context.
     */
    static QJsonArray toJsonArray(const QList<FailureLabel>& labels) {
        QJsonArray arr;
        for (const auto& label : labels) {
            arr.append(label.toJson());
        }
        return arr;
    }
};

// ============================================================================
// LEGACY STRING-BASED MODES (for backward compatibility)
// ============================================================================

namespace FailureModes {
    const QString SINGULAR_JACOBIAN = "Singular Jacobian";
    const QString FLOATING_NODE = "Floating Node";
    const QString CUTSET_VIOLATION = "Cutset Violation";
    const QString ZERO_DIAGONAL = "Zero Diagonal";
    const QString NON_CONVERGENCE = "Non-Convergence";
    const QString GMIN_FALLBACK = "GMIN Stepping Fallback";
    const QString SOURCE_STEPPING_FALLBACK = "Source Stepping Fallback";
    const QString NO_DC_PATH = "No DC Path to Ground";
    const QString STIFF_TRANSIENT = "Stiff Transient";
    const QString TIMESTEP_REBOUND = "Time-step Rebound";
    const QString ENERGY_GROWTH = "Energy Growth Violation";
    const QString NAN_DETECTED = "NaN/Inf Detected";

    inline bool isKnownFailure(const QString& type) {
        static const QSet<QString> known = {
            SINGULAR_JACOBIAN, FLOATING_NODE, CUTSET_VIOLATION, ZERO_DIAGONAL,
            NON_CONVERGENCE, GMIN_FALLBACK, SOURCE_STEPPING_FALLBACK, NO_DC_PATH,
            STIFF_TRANSIENT, TIMESTEP_REBOUND, ENERGY_GROWTH, NAN_DETECTED
        };
        return known.contains(type);
    }
}

} // namespace AIAPI
