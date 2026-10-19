#pragma once
#include <QString>
#include <QSet>
#include <QVariantMap>
#include <QVariantList>
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
    ERR,      // Simulation failed
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
            return FailureSeverity::ERR;
        case FailureType::SINGULAR_JACOBIAN:
        case FailureType::OVERFLOW_DETECTED:
        case FailureType::VOLTAGE_SOURCE_LOOP:
        case FailureType::CURRENT_SOURCE_CUTSET:
        case FailureType::DISCONNECTED_COMPONENT:
            return FailureSeverity::CRITICAL;
        default:
            return FailureSeverity::ERR;
    }
}

struct FailureLabel {
    FailureType type = FailureType::NONE;
    FailureSeverity severity = FailureSeverity::INFO;
    QString nodeOrComponent;
    QString message;
    int iteration = -1;
    double residual = 0.0;
    double timestamp = 0.0;
    QVector<QString> involvedNodes;
    QVariantMap debugContext;  // Changed from QJsonObject

    QVariantMap toMap() const {
        QVariantMap obj;
        obj["type"] = static_cast<int>(type);
        obj["typeString"] = failureTypeToString(type);
        obj["severity"] = static_cast<int>(severity);
        obj["nodeOrComponent"] = nodeOrComponent;
        obj["message"] = message;
        obj["iteration"] = iteration;
        obj["residual"] = residual;
        obj["timestamp"] = timestamp;
        obj["debugContext"] = debugContext;
        
        QVariantList nodesList;
        for(const auto& n : involvedNodes) nodesList.append(n);
        obj["involvedNodes"] = nodesList;
        
        return obj;
    }
    
    static FailureLabel fromMap(const QVariantMap& obj) {
        FailureLabel label;
        label.type = static_cast<FailureType>(obj["type"].toInt());
        label.severity = static_cast<FailureSeverity>(obj["severity"].toInt());
        label.nodeOrComponent = obj["nodeOrComponent"].toString();
        label.message = obj["message"].toString();
        label.iteration = obj.value("iteration", -1).toInt();
        label.residual = obj.value("residual", 0.0).toDouble();
        label.timestamp = obj.value("timestamp", 0.0).toDouble();
        label.debugContext = obj["debugContext"].toMap();
        
        QVariantList nodesArr = obj["involvedNodes"].toList();
        for(const auto& val : nodesArr) label.involvedNodes.append(val.toString());
        
        return label;
    }
    
    // Legacy support/helper if needed (but removing JSON dependency is key)
};

class FailureLabeler {
public:
    static QList<FailureLabel> labelFailures(const QVariantMap& solverStats,
                                              const QVariantMap& netlistState);
    
    static QVariantList toVariantList(const QList<FailureLabel>& labels) {
        QVariantList arr;
        for (const auto& label : labels) {
            arr.append(label.toMap());
        }
        return arr;
    }
};

} // namespace AIAPI
