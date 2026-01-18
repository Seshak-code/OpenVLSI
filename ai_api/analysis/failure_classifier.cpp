#include "failure_classifier.h"
#include <cmath>
#include <limits>

namespace AIAPI {

QVector<TraceFailure> FailureClassifier::analyzeConvergenceFailure(
    const Csr_matrix& matrix,
    const std::vector<double>& rhs,
    const std::vector<double>& solution,
    const SolverResult& result,
    double currentTime
) {
    QVector<TraceFailure> failures;

    // 1. Check for NaN/Inf in solution (Explosion)
    if (checkNaN(solution)) {
        failures.append({
            FailureModes::NAN_DETECTED,
            "Solver returned NaN/Inf values.",
            currentTime,
            {}, // TODO: Identify which nodes are NaN
            {}
        });
        // NaN usually implies singularity or bad Jacobian
    }

    // 2. Check for Singular Jacobian / Zero Diagonals
    QVector<QString> floatingNodes;
    if (checkFloatingNodes(matrix, floatingNodes)) {
        failures.append({
            FailureModes::FLOATING_NODE,
            "Diagonal element is zero or near-zero.",
            currentTime,
            floatingNodes,
            QJsonObject{{"threshold", 1e-12}}
        });
    }

    // 3. Generic Non-Convergence
    if (!result.converged && failures.empty()) {
        failures.append({
            FailureModes::NON_CONVERGENCE,
            QString("Newton-Raphson failed after %1 iterations (Residual: %2)")
                   .arg(result.iterations).arg(result.finalResidual),
            currentTime,
            {}, 
            QJsonObject{{"iterations", result.iterations}, {"residual", result.finalResidual}}
        });
    }

    return failures;
}

QVector<TraceFailure> FailureClassifier::analyzeTransientFailure(
    double timeStep,
    int attemptCount,
    bool energyViolation
) {
    QVector<TraceFailure> failures;

    if (energyViolation) {
        failures.append({
            FailureModes::ENERGY_GROWTH,
            "Lyapunov energy function increased during passive step.",
            0.0, // Timestamp context usually added by caller
            {},
            QJsonObject{}
        });
    }

    if (timeStep < 1e-15) { // Arbitrary small dt
        failures.append({
            FailureModes::TIMESTEP_REBOUND,
            "Time-step collapsed to machine precision limit.",
            0.0,
            {},
            QJsonObject{{"attempts", attemptCount}}
        });
    }

    return failures;
}

bool FailureClassifier::checkNaN(const std::vector<double>& data) {
    for (double v : data) {
        if (std::isnan(v) || std::isinf(v)) return true;
    }
    return false;
}

bool FailureClassifier::checkFloatingNodes(const Csr_matrix& matrix, QVector<QString>& outNodes) {
    bool found = false;
    // Inspect diagonals
    // Using simple heuristic: if diagonal is effectively zero compared to row norm
    // Note: This requires matrix.row_pointer structure
    
    for (int i = 0; i < matrix.rows; ++i) {
        double diagVal = 0.0;
        bool diagFound = false;
        
        int start = matrix.row_pointer[i];
        int end = matrix.row_pointer[i+1];
        
        for (int k = start; k < end; ++k) {
            if (matrix.col_indices[k] == i) {
                diagVal = matrix.values[k];
                diagFound = true;
                break;
            }
        }
        
        if (!diagFound || std::abs(diagVal) < 1e-12) {
            // Found a potentially floating node (Node Index i)
            // Ideally we map index 'i' back to a Node Name via Netlist, but here we just store ID
            outNodes.append(QString::number(i));
            found = true;
        }
    }
    return found;
}

} // namespace AIAPI
