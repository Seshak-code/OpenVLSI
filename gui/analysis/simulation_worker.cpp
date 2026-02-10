#include "simulation_worker.h"
#include <QDebug>
#include <QDateTime>
#include <QThread>

// Helper for parsing values with units (Hz, kHz, ms, us, etc.)
static double parseUnitValue(const QString& str, double defaultVal = 0.0) {
    if (str.isEmpty()) return defaultVal;
    
    QString s = str.trimmed();
    double multiplier = 1.0;
    int suffixLen = 0;
    
    // Frequency
    if (s.endsWith("GHz", Qt::CaseInsensitive)) { multiplier = 1e9; suffixLen = 3; }
    else if (s.endsWith("MHz", Qt::CaseInsensitive)) { multiplier = 1e6; suffixLen = 3; }
    else if (s.endsWith("kHz", Qt::CaseInsensitive)) { multiplier = 1e3; suffixLen = 3; }
    else if (s.endsWith("Hz", Qt::CaseInsensitive)) { multiplier = 1.0; suffixLen = 2; }
    // Time
    else if (s.endsWith("ms", Qt::CaseInsensitive)) { multiplier = 1e-3; suffixLen = 2; }
    else if (s.endsWith("us", Qt::CaseInsensitive)) { multiplier = 1e-6; suffixLen = 2; }
    else if (s.endsWith("ns", Qt::CaseInsensitive)) { multiplier = 1e-9; suffixLen = 2; }
    // else if (s.endsWith("s", Qt::CaseInsensitive)) { multiplier = 1.0; suffixLen = 1; } // Ambiguous with others? "ms" ends with "s" but "ms" checked first. 
    
    // Check "s" last, but careful not to match "Gs" if that existed (Gauss?). 
    // Just "s" usually means seconds if not matched by ms/us/ns.
    if (suffixLen == 0 && s.endsWith("s", Qt::CaseInsensitive) && !s.endsWith("s", Qt::CaseInsensitive)) { 
        // Logic error in my thinking. "s" check should be checking strict suffix.
        // Actually s.endsWith("s") is true for "ms".
        // But we already handled "ms", "us", etc.
        // So safe to check "s" here?
        // "3s" -> not ms/us/ns.
        multiplier = 1.0; suffixLen = 1;
    }
    // Re-do logic cleanly
    if (suffixLen == 0 && s.endsWith("s", Qt::CaseInsensitive)) {
        multiplier = 1.0; suffixLen = 1;
    }

    if (suffixLen > 0) {
        bool ok;
        double val = s.left(s.length() - suffixLen).toDouble(&ok);
        if (ok) return val * multiplier;
    }
    
    // Fallback or no unit
    bool ok;
    double val = s.toDouble(&ok);
    return ok ? val : defaultVal;
}

SimulationWorker::SimulationWorker(QObject* parent) : QObject(parent) {

    // Constructor runs in the creating thread (Main)
}

SimulationWorker::~SimulationWorker() {
    stopAndJoin();
}

void SimulationWorker::stopAndJoin() {
    stop();
    // In a real thread join scenario, QThread::wait() handled by owner
}

void SimulationWorker::stop() {
    stopRequested_.store(true, std::memory_order_relaxed);
}

bool SimulationWorker::isStopRequested() const {
    return stopRequested_.load(std::memory_order_relaxed);
}

void SimulationWorker::startDC(TensorNetlist netlist, Simulation::SimulationProfile profile, acutesim::physics::RunMode mode) {
    if (running_.exchange(true)) {
        emit logMessage("[Worker] Simulation already in progress.");
        return;
    }
    stopRequested_.store(false);
    
    // Set Execution Mode
    // sim_.execMode is part of CircuitSim. We map RunMode/Profile to CircuitSim internals if needed.
    // For now, assume LEGACY_AOS or GPU based on external config? 
    // Wait, SimulationWorkspace set execMode based on ComboBox.
    // We didn't pass execMode in the signature!
    // Added acutesim::physics::RunMode, but that is FULL/QUICK.
    // We need ComputeEngine enum too? Or stick to default for now?
    // Implementation plan didn't specify ComputeEngine propagation.
    // Let's assume LEGACY for now or deduce from profile.
    
    QList<acutesim::physics::CornerConfig> runCorners = profile.corners;
    if (runCorners.isEmpty()) {
        runCorners.append(acutesim::physics::CornerConfig::Nominal());
    }
    
    emit logMessage(QString("[Worker] Starting DC Analysis (%1 corners)...").arg(runCorners.size()));
    
    bool allSuccess = true;
    
    for (const auto& corner : runCorners) {
        if (isStopRequested()) break;
        
        sim_.applyCorner(netlist, corner);
        emit logMessage(QString("[Worker] Corner: %1").arg(QString::fromStdString(corner.name)));
        
        SolverStep result = sim_.solveDC(netlist);
        
        if (result.stats.converged) {
            // Build Result
            QJsonObject jsonRes;
            jsonRes["type"] = "DC";
            jsonRes["corner"] = QString::fromStdString(corner.name);

            QJsonArray voltages;
            for(double v : result.nodeVoltages) voltages.append(v);
            jsonRes["voltages"] = voltages;

            // Solver Stats (for trust badge and run tracking)
            QJsonObject stats;
            stats["iterations"] = result.stats.iterations;
            stats["residual"] = result.stats.residual;
            stats["converged"] = result.stats.converged;
            stats["method"] = QString::fromStdString(result.stats.method);
            stats["integrationMethod"] = QString::fromStdString(result.stats.integrationMethod);
            stats["gminSteppingUsed"] = result.stats.gminSteppingUsed;
            stats["sourceSteppingUsed"] = result.stats.sourceSteppingUsed;
            stats["failureType"] = QString::fromStdString(
                convergenceFailureToString(result.stats.failureType));
            if (!result.stats.convergenceJustification.empty()) {
                stats["convergenceJustification"] = QString::fromStdString(
                    result.stats.convergenceJustification);
            }
            jsonRes["stats"] = stats;

            // Probes
            QJsonObject probeVals;
            for (const auto& probe : netlist.globalBlock.probes) {
                double v_pos = (probe.nodePositive > 0 && probe.nodePositive <= (int)result.nodeVoltages.size()) ? result.nodeVoltages[probe.nodePositive-1] : 0.0;
                double v_neg = (probe.nodeNegative > 0 && probe.nodeNegative <= (int)result.nodeVoltages.size()) ? result.nodeVoltages[probe.nodeNegative-1] : 0.0;
                probeVals[QString::fromStdString(probe.name)] = v_pos - v_neg;
            }
            jsonRes["probes"] = probeVals;

            emit resultsReady(jsonRes);
        } else {
            allSuccess = false;

            // Map ConvergenceFailureType to ERC ViolationType
            ERC::ViolationType vType = ERC::ViolationType::SOLVER_DIVERGENCE;
            switch (result.stats.failureType) {
                case ConvergenceFailureType::SINGULAR_MATRIX:
                    vType = ERC::ViolationType::SOLVER_SINGULAR_MATRIX;
                    break;
                case ConvergenceFailureType::LIMIT_CYCLE:
                    vType = ERC::ViolationType::SOLVER_LIMIT_CYCLE;
                    break;
                case ConvergenceFailureType::SLOW_CONVERGENCE:
                    vType = ERC::ViolationType::SOLVER_SLOW_CONVERGENCE;
                    break;
                case ConvergenceFailureType::EXPONENTIAL_OVERFLOW:
                    vType = ERC::ViolationType::SOLVER_EXPONENTIAL_OVERFLOW;
                    break;
                case ConvergenceFailureType::DIVERGENCE:
                default:
                    vType = ERC::ViolationType::SOLVER_DIVERGENCE;
                    break;
            }

            // Fallback: check error_detail string if failureType is NONE
            if (result.stats.failureType == ConvergenceFailureType::NONE) {
                QString detail = QString::fromStdString(result.stats.error_detail);
                if (detail.contains("Singular Matrix", Qt::CaseInsensitive)) {
                    vType = ERC::ViolationType::SOLVER_SINGULAR_MATRIX;
                }
            }

            QString message = QString("Solver Failed: %1 (Corner %2)")
                .arg(QString::fromStdString(result.stats.convergenceJustification.empty()
                    ? result.stats.error_detail
                    : result.stats.convergenceJustification))
                .arg(QString::fromStdString(corner.name));

            ERC::Violation v(vType, message, QPoint(-9999, -9999));
            emit solverViolation(v);
        }
    }
    
    running_ = false;
    emit simulationFinished();
}

void SimulationWorker::startTransient(TensorNetlist netlist, Simulation::SimulationProfile profile, acutesim::physics::RunMode mode) {
    if (running_.exchange(true)) {
        emit logMessage("[Worker] Simulation already in progress.");
        return;
    }
    stopRequested_.store(false);
    
    QList<acutesim::physics::CornerConfig> runCorners = profile.corners;
    if (runCorners.isEmpty()) {
        runCorners.append(acutesim::physics::CornerConfig::Nominal());
    }
    
    // Transient only runs first corner for now (as per Workspace logic)
    auto corner = runCorners.first();
    sim_.applyCorner(netlist, corner);
    
    emit logMessage(QString("[Worker] Starting Transient Analysis (Stop: %1)...").arg(profile.parameters.value("stopTime", "1ms").toString()));
    
    double stopTime = parseUnitValue(profile.parameters.value("stopTime", "1ms").toString(), 0.001);

    double currentTime = 0.0;
    double timeStep = 1e-5; // Default step

    // Tracking
    int stepCount = 0;
    int totalIterations = 0;
    double maxResidual = 0.0;
    bool failed = false;

    while (currentTime < stopTime && !isStopRequested()) {
        SolverStep result = sim_.stepTransient(netlist, timeStep, currentTime);

        // Use the solver's reported time (accounts for adaptive stepping)
        currentTime = result.time;
        stepCount++;
        totalIterations += result.stats.iterations;
        if (result.stats.residual > maxResidual) {
            maxResidual = result.stats.residual;
        }

        if (!result.stats.converged) {
            QString reason = result.stats.error_detail.empty() ? "Divergence" : QString::fromStdString(result.stats.error_detail);
            ERC::ViolationType vType = ERC::ViolationType::SOLVER_DIVERGENCE;
            if (reason.contains("Singular Matrix", Qt::CaseInsensitive)) {
                vType = ERC::ViolationType::SOLVER_SINGULAR_MATRIX;
            }

            ERC::Violation v(vType, QString("Transient Failed at t=%1s: %2").arg(currentTime).arg(reason), QPoint(-9999, -9999));
            emit solverViolation(v);
            failed = true;
            break;
        }

        // Emit per-step data
        QJsonObject point;
        point["type"] = "TransientPoint";
        point["time"] = currentTime;

        QJsonArray voltages;
        for(double v : result.nodeVoltages) voltages.append(v);
        point["voltages"] = voltages;

        QJsonObject probeVals;
        for (const auto& probe : netlist.globalBlock.probes) {
             double v_pos = (probe.nodePositive > 0 && probe.nodePositive <= (int)result.nodeVoltages.size()) ? result.nodeVoltages[probe.nodePositive-1] : 0.0;
             double v_neg = (probe.nodeNegative > 0 && probe.nodeNegative <= (int)result.nodeVoltages.size()) ? result.nodeVoltages[probe.nodeNegative-1] : 0.0;
             probeVals[QString::fromStdString(probe.name)] = v_pos - v_neg;
        }
        point["probes"] = probeVals;

        emit resultsReady(point);

        // Logging throttle
        if (stepCount % 100 == 0) {
             emit streamMessage(QString("t = %1 us").arg(currentTime * 1e6, 0, 'f', 2));
        }
    }

    if (isStopRequested()) {
        emit logMessage("[Worker] Simulation stopped by user.");
    } else if (!failed) {
        // Emit summary result with stats for run tracking and trust badge
        QJsonObject summary;
        summary["type"] = "TransientSummary";
        summary["steps"] = stepCount;
        summary["finalTime"] = currentTime;

        QJsonObject stats;
        stats["iterations"] = totalIterations;
        stats["residual"] = maxResidual;
        stats["converged"] = true;
        stats["method"] = QString("Newton-Raphson");
        stats["integrationMethod"] = QString("Adaptive");
        stats["gminSteppingUsed"] = false;
        stats["sourceSteppingUsed"] = false;
        summary["stats"] = stats;

        emit resultsReady(summary);
        emit logMessage(QString("[Worker] Transient Analysis Completed (%1 steps).").arg(stepCount));
    }

    running_ = false;
    emit simulationFinished();
}

void SimulationWorker::startAC(TensorNetlist netlist, Simulation::SimulationProfile profile, acutesim::physics::RunMode mode) {
    Q_UNUSED(mode);
    if (running_.exchange(true)) {
        emit logMessage("[Worker] Simulation already in progress.");
        return;
    }
    stopRequested_.store(false);

    // 1. Run DC solve first for operating point
    emit logMessage("[Worker] Starting AC Analysis (DC first)...");
    SolverStep dcResult = sim_.solveDC(netlist);

    if (!dcResult.stats.converged) {
        ERC::Violation v(ERC::ViolationType::SOLVER_DIVERGENCE,
            QString("AC: DC operating point failed: %1")
                .arg(QString::fromStdString(dcResult.stats.error_detail)),
            QPoint(-9999, -9999));
        emit solverViolation(v);
        running_ = false;
        emit simulationFinished();
        return;
    }

    // 2. Extract frequency parameters
    double fStart = 1.0, fStop = 1e9;
    int points = 200;

    fStart = parseUnitValue(profile.parameters.value("fStart", "1Hz").toString(), 1.0);
    fStop = parseUnitValue(profile.parameters.value("fStop", "1GHz").toString(), 1e9);

    points = profile.parameters.value("points", 200).toInt();

    emit logMessage(QString("[Worker] AC Sweep: %1 Hz to %2 Hz (%3 points)")
        .arg(fStart).arg(fStop).arg(points));

    // 3. Run AC sweep
    ACSolver::ACResult acResult = ACSolver::solveAC(
        netlist, dcResult.nodeVoltages, fStart, fStop, points);

    // 4. Build JSON result
    QJsonObject jsonRes;
    jsonRes["type"] = "AC";

    QJsonArray freqs;
    for (double f : acResult.frequencies) freqs.append(f);
    jsonRes["frequencies"] = freqs;

    // Per-node magnitude and phase arrays
    int n = netlist.numGlobalNodes;
    for (int node = 0; node < n; ++node) {
        QJsonArray mag, phase;
        for (int fi = 0; fi < points; ++fi) {
            if (fi < (int)acResult.nodeVoltages.size() && node < (int)acResult.nodeVoltages[fi].size()) {
                mag.append(std::abs(acResult.nodeVoltages[fi][node]));
                phase.append(std::arg(acResult.nodeVoltages[fi][node]) * 180.0 / M_PI);
            }
        }
        jsonRes[QString("node%1_mag").arg(node + 1)] = mag;
        jsonRes[QString("node%1_phase").arg(node + 1)] = phase;
    }

    emit resultsReady(jsonRes);
    emit logMessage(QString("[Worker] AC Analysis Completed (%1 frequency points).").arg(points));

    running_ = false;
    emit simulationFinished();
}

void SimulationWorker::startNoise(TensorNetlist netlist, Simulation::SimulationProfile profile, acutesim::physics::RunMode mode) {
    Q_UNUSED(mode);
    if (running_.exchange(true)) {
        emit logMessage("[Worker] Simulation already in progress.");
        return;
    }
    stopRequested_.store(false);

    // 1. Run DC solve first
    emit logMessage("[Worker] Starting Noise Analysis (DC first)...");
    SolverStep dcResult = sim_.solveDC(netlist);

    if (!dcResult.stats.converged) {
        ERC::Violation v(ERC::ViolationType::SOLVER_DIVERGENCE,
            QString("Noise: DC operating point failed: %1")
                .arg(QString::fromStdString(dcResult.stats.error_detail)),
            QPoint(-9999, -9999));
        emit solverViolation(v);
        running_ = false;
        emit simulationFinished();
        return;
    }

    // 2. Extract parameters
    double fStart = 1.0, fStop = 1e9;
    int points = 200;
    int outputNode = profile.parameters.value("outputNode", 1).toInt();
    int inputNode = profile.parameters.value("inputNode", 0).toInt();
    double temp_K = profile.parameters.value("temperature_K", 300.15).toDouble();

    QString fStartStr = profile.parameters.value("fStart", "1Hz").toString();
    fStart = parseUnitValue(fStartStr, 1.0);

    QString fStopStr = profile.parameters.value("fStop", "1GHz").toString();
    fStop = parseUnitValue(fStopStr, 1e9);

    points = profile.parameters.value("points", 200).toInt();

    emit logMessage(QString("[Worker] Noise Analysis: output=node%1, %2 Hz to %3 Hz")
        .arg(outputNode).arg(fStart).arg(fStop));

    // 3. Run noise analysis
    ACSolver::NoiseResult noiseResult = ACSolver::solveNoise(
        netlist, dcResult.nodeVoltages,
        outputNode, inputNode,
        fStart, fStop, points, temp_K);

    // 4. Build JSON result
    QJsonObject jsonRes;
    jsonRes["type"] = "Noise";
    jsonRes["outputNode"] = outputNode;
    jsonRes["inputNode"] = inputNode;
    jsonRes["integrated_noise_vrms"] = noiseResult.integrated_noise_vrms;

    QJsonArray freqs, psd;
    for (size_t i = 0; i < noiseResult.frequencies.size(); ++i) {
        freqs.append(noiseResult.frequencies[i]);
        psd.append(noiseResult.output_noise_psd[i]);
    }
    jsonRes["frequencies"] = freqs;
    jsonRes["output_noise_psd"] = psd;

    emit resultsReady(jsonRes);
    emit logMessage(QString("[Worker] Noise Analysis Completed. Integrated noise: %1 Vrms")
        .arg(noiseResult.integrated_noise_vrms, 0, 'e', 3));

    running_ = false;
    emit simulationFinished();
}
