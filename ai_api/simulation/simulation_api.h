#pragma once
#include <QString>
#include <QJsonObject>
#include <vector>

/**
 * AI API: Simulation & Analysis Control
 * 
 * Provides programmatic access to all simulation engines for GenAI agents.
 */

namespace AIAPI {

// ============================================================================
// SIMULATION CONTROL
// ============================================================================

struct SimulationSettings {
    QString analysisType;  // "DC", "AC", "TRAN", "NOISE"
    double timeStep;       // For transient
    double stopTime;
    double startFreq;      // For AC
    double stopFreq;
    int pointsPerDecade;
    QString integrationMethod;  // "Trapezoidal", "Gear2"
    double reltol;
    double abstol;
};

/**
 * Configure simulation parameters.
 */
bool setSimulationSettings(const SimulationSettings& settings);

/**
 * Run DC operating point analysis.
 * Returns: Node voltages, device currents, power dissipation
 */
QJsonObject runDCAnalysis();

/**
 * Run transient analysis.
 * Returns: Time-domain waveforms for all probes
 */
QJsonObject runTransientAnalysis();

/**
 * Run AC small-signal analysis.
 * Returns: Frequency-domain response (magnitude, phase)
 */
QJsonObject runACAnalysis();

/**
 * Run noise analysis.
 */
QJsonObject runNoiseAnalysis(const QString& outputNode, const QString& inputSource);

/**
 * Stop a running simulation.
 */
bool stopSimulation();

// ============================================================================
// PROBE MANAGEMENT
// ============================================================================

struct ProbeConfig {
    QString probeType;   // "voltage", "current", "power"
    QString nodeId;      // For voltage probes
    QString componentId; // For current/power probes
    QString label;       // Display name
};

/**
 * Place a probe for measurement.
 */
QString placeProbe(const ProbeConfig& config);

/**
 * Remove a probe.
 */
bool removeProbe(const QString& probeId);

/**
 * Get all active probes.
 */
QVector<ProbeConfig> getActiveProbes();

// ============================================================================
// MEASUREMENTS
// ============================================================================

struct MeasurementCommand {
    QString type;        // "TRISE", "TFALL", "DELAY", "POWER_AVG", "FREQ"
    QString signal;      // Probe ID or node name
    QJsonObject params;  // Threshold, start/stop times, etc.
};

/**
 * Define a measurement (like SPICE .measure).
 */
QString defineMeasurement(const MeasurementCommand& cmd);

/**
 * Get measurement results after simulation.
 */
QJsonObject getMeasurementResults();

// ============================================================================
// PARAMETER SWEEPS
// ============================================================================

struct SweepConfig {
    QString parameterName;  // "R1.resistance", "VDD.voltage"
    double startValue;
    double stopValue;
    int steps;
    QString sweepType;      // "linear", "log", "list"
};

/**
 * Run parametric sweep.
 */
QJsonObject runParametricSweep(const SweepConfig& config);

/**
 * Run Monte Carlo analysis.
 */
QJsonObject runMonteCarlo(int trials, double tolerance);

/**
 * Run corner analysis (TT, FF, SS, etc.).
 */
QJsonObject runCornerAnalysis(const QStringList& corners);

// ============================================================================
// CONVERGENCE DIAGNOSTICS
// ============================================================================

struct ConvergenceReport {
    bool converged;
    int iterations;
    double residual;
    QString failureReason;
    QStringList suggestions;  // "Reduce timestep", "Add Gmin"
};

/**
 * Get convergence diagnostics for last simulation.
 */
ConvergenceReport getConvergenceReport();

/**
 * Auto-fix convergence issues.
 */
bool autoFixConvergence();

} // namespace AIAPI
