#pragma once
#include "simulation_trace.h"
#include "failure_taxonomy.h"
#include "linalg.h" 
#include "circuitsim.h" // For SolverResult

namespace AIAPI {

/**
 * Step 2.2: Automated Labeling Pipeline
 * Analysis engine that inspects solver state to tag failure modes.
 */
class FailureClassifier {
public:
    // Main entry point: Analyze a failed simulation step and return failure tags
    static QVector<FailureLabel> analyzeConvergenceFailure(
        const Csr_matrix& matrix,
        const std::vector<double>& rhs,
        const std::vector<double>& solution,
        const SolverResult& result,
        double currentTime
    );

    static QVector<FailureLabel> analyzeTransientFailure(
        double timeStep,
        int attemptCount,
        bool energyViolation
    );

private:
    // Heuristics
    static bool checkSingularity(const Csr_matrix& matrix);
    static bool checkFloatingNodes(const Csr_matrix& matrix, QVector<QString>& outNodes);
    static bool checkNaN(const std::vector<double>& data);
};

} // namespace AIAPI
