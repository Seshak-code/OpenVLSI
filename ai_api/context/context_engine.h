#pragma once
#include <QString>
#include <QJsonObject>
#include <optional>
#include "../policy/ai_policy.h"
#include "../simulation/simulation_trace.h"

namespace AIAPI {

class ContextEngine {
public:
    /**
     * Generates a comprehensive system prompt describing the current circuit state.
     * This includes:
     * - Top-level stats (e.g. "Circuit with 15 components")
     * - Textual Netlist (e.g. "R1 connects Node 1 to Node 2")
     * - Parameter details (e.g. "M1 is an NMOS with W=10u")
     * - Visual context (e.g. "Components are placed in a cascade structure")
     */
    static QString generateSystemPrompt();
    
    /**
     * Generates a compact JSON summary token-optimized for LLM context windows.
     */
    static QJsonObject getContextJson();
    
    /**
     * Analyzes the circuit topology to discover signal paths from sources to sinks.
     * Returns a list of paths, each containing:
     * - "source": The driving component (e.g., "V1", "I1")
     * - "sink": The terminating node (e.g., "GND", "R_load")
     * - "path": Ordered list of component IDs in the signal chain
     * - "probePoints": Recommended measurement nodes along the path
     */
    static QJsonArray analyzeSignalPaths();
    
    /**
     * Provides intelligent debug suggestions based on circuit topology.
     * Returns a list of suggestions, each containing:
     * - "node": The node ID or component terminal
     * - "reason": Why this is a good probe point (e.g., "High-gain stage input")
     * - "priority": 1 (critical) to 3 (informational)
     */
    static QJsonArray getDebugSuggestions();
    
    // =========================================================================
    // Phase 1.26 Step 4: AI Context Representation Layer
    // =========================================================================
    
    /**
     * 4.1: Serialize topology, device tables, and DC/Transient endpoints.
     * Returns structured chunks suitable for RAG embedding or prompt injection.
     * Includes:
     * - Device Tables (Resistors, Capacitors, BJTs, MOSFETs, Diodes)
     * - DC Operating Point Summary (Node Voltages, Branch Currents)
     * - Transient Endpoints (Final time, final voltages)
     */
    static QJsonObject serializeTopologyChunks();
    
    /**
     * 4.2: Inject probe values and historical error metrics.
     * Returns:
     * - Named probe values (e.g., "Vout": 2.5V)
     * - Historical KCL residuals and convergence failures
     */
    static QJsonObject serializeProbeContext();
    
    /**
     * 4.3: Incorporate solver decisions and flags.
     * Returns:
     * - Integration method (Trapezoidal, Gear2)
     * - GMIN stepping state
     * - LTE thresholds and rollback history
     */
    static QJsonObject serializeSolverDecisions();

private:
    static QString describeComponent(const QJsonObject& comp);
    static QString inferTopologyIntent(const QJsonObject& state);
    static QString getSpatialContext();
    
    // Phase 1.7.10: AI Policy Enforcement
    static AIPolicy currentPolicy_;

    // Phase 1.26: Last simulation trace cache
    static std::optional<SimulationTrace> lastTrace_;
    
public:
    static void setPolicy(const AIPolicy& policy) { currentPolicy_ = policy; }
    static AIPolicy getPolicy() { return currentPolicy_; }

    // Phase 1.26: Last simulation trace for live context wiring
    static void setLastTrace(const SimulationTrace& trace) { lastTrace_ = trace; }
    static std::optional<SimulationTrace> getLastTrace() { return lastTrace_; }
    
    /**
     * Applies policy to an AI response:
     * 1. Validates citations if required.
     * 2. Prepends solver evidence if "Raw Evidence First" is enabled.
     */
    static QString applyPolicy(const QString& aiResponse, const QJsonObject& solverEvidence);
};

} // namespace AIAPI
