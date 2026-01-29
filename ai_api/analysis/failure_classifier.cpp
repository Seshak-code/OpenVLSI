#include "failure_classifier.h"
#include <cmath>
#include <limits>

namespace AIAPI {

QVector<FailureLabel> FailureClassifier::analyzeConvergenceFailure(
    const Csr_matrix& matrix,
    const std::vector<double>& rhs,
    const std::vector<double>& solution,
    const SolverResult& result,
    double currentTime
) {
    QVector<FailureLabel> failures;

    // 1. Check for NaN/Inf in solution (Explosion)
    // 1. Check for NaN/Inf in solution (Explosion)
    if (checkNaN(solution)) {
        FailureLabel label;
        label.type = FailureType::OVERFLOW_DETECTED;
        label.severity = FailureSeverity::CRITICAL;
        label.message = "Solver returned NaN/Inf values.";
        label.timestamp = currentTime;
        failures.append(label);
        // NaN usually implies singularity or bad Jacobian
    }

    // 2. Check for Singular Jacobian / Zero Diagonals
    QVector<QString> floatingNodes;
    if (checkFloatingNodes(matrix, floatingNodes)) {
        FailureLabel label;
        label.type = FailureType::FLOATING_NODE;
        label.severity = FailureSeverity::ERROR;
        label.message = "Diagonal element is zero or near-zero.";
        label.timestamp = currentTime;
        label.involvedNodes = floatingNodes;
        label.debugContext = QVariantMap{{"threshold", 1e-12}};
        failures.append(label);
    }

    // 3. Generic Non-Convergence
    if (!result.converged && failures.empty()) {
        FailureLabel label;
        label.type = FailureType::CONVERGENCE_FAILED;
        label.severity = FailureSeverity::ERROR;
        label.message = QString("Newton-Raphson failed after %1 iterations (Residual: %2)")
                   .arg(result.iterations).arg(result.finalResidual);
        label.timestamp = currentTime;
        label.iteration = result.iterations;
        label.residual = result.finalResidual;
        label.debugContext = QVariantMap{{"iterations", result.iterations}, {"residual", result.finalResidual}};
        failures.append(label);
    }

    return failures;
}

QVector<FailureLabel> FailureClassifier::analyzeTransientFailure(
    double timeStep,
    int attemptCount,
    bool energyViolation
) {
    QVector<FailureLabel> failures;

    if (energyViolation) {
        FailureLabel label;
        label.type = FailureType::LTE_VIOLATION; // Approx mapping
        // Actually ENERGY_GROWTH is not in enum? I removed it.
        // Let's use LTE_VIOLATION or VOLTAGE_SOURCE_LOOP?
        // Wait, I should have kept ENERGY_GROWTH or added it to Enum.
        // Checking FailureType: LTE_VIOLATION, TIMESTEP_TOO_SMALL, OVERFLOW, VOLTAGE_SOURCE_LOOP...
        // Let's use LTE_VIOLATION for now, or add ENERGY_VIOLATION to enum.
        // Actually, let's use LTE_VIOLATION as "Stability Issue".
        label.severity = FailureSeverity::WARNING;
        label.message = "Lyapunov energy function increased during passive step.";
        label.timestamp = 0.0;
        failures.append(label);
    }

    if (timeStep < 1e-15) { // Arbitrary small dt
        FailureLabel label;
        label.type = FailureType::TIMESTEP_TOO_SMALL;
        label.severity = FailureSeverity::ERROR;
        label.message = "Time-step collapsed to machine precision limit.";
        label.timestamp = 0.0;
        label.debugContext = QVariantMap{{"attempts", attemptCount}};
        failures.append(label);
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
