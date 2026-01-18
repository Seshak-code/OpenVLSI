#pragma once
#include <QString>
#include <QSet>

namespace AIAPI {

/**
 * Step 2.1: Failure Vocabulary
 * Controlled vocabulary for simulation failure modes to ensure consistent RAG indexing.
 */
namespace FailureModes {

    // Matrix / Topology Issues
    const QString SINGULAR_JACOBIAN = "Singular Jacobian";
    const QString FLOATING_NODE = "Floating Node";
    const QString CUTSET_VIOLATION = "Cutset Violation"; // e.g. Current sources in series
    const QString ZERO_DIAGONAL = "Zero Diagonal";

    // Convergence Issues
    const QString NON_CONVERGENCE = "Non-Convergence";
    const QString GMIN_FALLBACK = "GMIN Stepping Fallback";
    const QString SOURCE_STEPPING_FALLBACK = "Source Stepping Fallback";
    const QString NO_DC_PATH = "No DC Path to Ground";

    // Transient Issues
    const QString STIFF_TRANSIENT = "Stiff Transient";
    const QString TIMESTEP_REBOUND = "Time-step Rebound"; // dt cut repeatedly
    const QString ENERGY_GROWTH = "Energy Growth Violation"; // Non-passive behavior
    const QString NAN_DETECTED = "NaN/Inf Detected";

    // Helper to check if a string is a known failure mode
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
