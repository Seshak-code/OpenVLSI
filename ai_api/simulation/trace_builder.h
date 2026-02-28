#pragma once

#include "simulation_trace.h"
#include "trace_chunker.h"
#include "../analysis/failure_classifier.h"
#include "../../gui/analysis/simulation_profile.h"
#include "acutesim_engine/netlist/circuit.h"
#include <QUuid>
#include <QDateTime>

namespace AIAPI {

/**
 * TraceBuilder - Factory for constructing SimulationTrace objects.
 * 
 * Bridges the gap between the solver's output (SimulationRun, TensorNetlist)
 * and the AI's input (SimulationTrace with RAG chunks).
 */
class TraceBuilder {
public:
    /**
     * Build a complete SimulationTrace from a finished simulation run.
     * 
     * @param run         The completed simulation run with status and metrics
     * @param netlist     The circuit netlist (for device mix extraction)
     * @param waveforms   Optional: Time-domain waveform data
     * @param timePoints  Optional: Time points corresponding to waveforms
     * @return            A fully populated SimulationTrace ready for RAG
     */
    static SimulationTrace fromRun(
        const Simulation::SimulationRun& run,
        const TensorNetlist& netlist,
        const QMap<QString, std::vector<double>>& waveforms = {},
        const std::vector<double>& timePoints = {}
    ) {
        SimulationTrace trace;
        
        // ==========================================================================
        // 1. IDENTITY
        // ==========================================================================
        trace.traceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        trace.topologyHash = QString::number(run.topologyHash, 16);
        trace.timestamp = QDateTime::currentSecsSinceEpoch();
        
        // ==========================================================================
        // 2. DEVICE MIX (from netlist)
        // ==========================================================================
        trace.deviceMix = extractDeviceMix(netlist.globalBlock);
        
        // ==========================================================================
        // 3. CONFIG PARAMS (from run)
        // ==========================================================================
        trace.configParams.solverType = getSolverType(run);
        trace.configParams.initialTimeStep = 1e-9; // TODO: Extract from profile
        trace.configParams.gminSteppingEnabled = run.trustGminUsed;
        trace.configParams.reltol = 1e-3; // Default; TODO: Extract from profile
        trace.configParams.abstol = 1e-12;
        trace.configParams.predictionEnabled = false;
        
        // ==========================================================================
        // 4. WAVEFORM DATA (if provided)
        // ==========================================================================
        trace.voltageSeries = waveforms;
        trace.timePoints = timePoints;
        
        // ==========================================================================
        // 5. FAILURE MODES (if failed)
        // ==========================================================================
        if (run.status != Simulation::SimulationRun::Status::CONVERGED) {
            trace.failureModes = extractFailures(run);
        }
        
        // ==========================================================================
        // 6. RAG CHUNKS (auto-generated)
        // ==========================================================================
        ChunkingStrategy strategy = trace.isFailure() 
            ? ChunkingStrategy::FAILURE_FOCUSED 
            : ChunkingStrategy::BALANCED;
        trace.ragChunks = TraceChunker::chunk(trace, strategy);
        
        return trace;
    }
    
    /**
     * Build a trace from raw solver output (for direct integration).
     */
    static SimulationTrace fromSolverOutput(
        const QString& topologyHash,
        const QMap<QString, int>& deviceMix,
        const std::vector<double>& nodeVoltages,
        bool converged,
        int iterations,
        double residual
    ) {
        SimulationTrace trace;
        trace.traceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        trace.topologyHash = topologyHash;
        trace.timestamp = QDateTime::currentSecsSinceEpoch();
        trace.deviceMix = deviceMix;
        
        // Default config
        trace.configParams.solverType = "LU";
        trace.configParams.reltol = 1e-3;
        trace.configParams.abstol = 1e-12;
        
        // Store final voltages as "waveform" with single point
        for (int i = 0; i < static_cast<int>(nodeVoltages.size()); ++i) {
            trace.voltageSeries[QString("Node%1").arg(i)] = {nodeVoltages[i]};
        }
        
        // Generate failure if not converged
        if (!converged) {
            TraceFailure failure;
            failure.failureType = "Convergence Failed";
            failure.description = QString("Newton-Raphson failed after %1 iterations (Residual: %2)")
                                      .arg(iterations).arg(residual, 0, 'e', 3);
            failure.timestamp = 0.0;
            trace.failureModes.append(failure);
        }
        
        // Generate RAG chunks
        trace.ragChunks = TraceChunker::chunk(trace, ChunkingStrategy::BALANCED);
        
        return trace;
    }

private:
    /**
     * Extract device counts from TensorBlock.
     */
    static QMap<QString, int> extractDeviceMix(const TensorBlock& block) {
        QMap<QString, int> mix;
        
        if (!block.resistors.empty()) mix["Resistor"] = static_cast<int>(block.resistors.size());
        if (!block.capacitors.empty()) mix["Capacitor"] = static_cast<int>(block.capacitors.size());
        if (!block.inductors.empty()) mix["Inductor"] = static_cast<int>(block.inductors.size());
        if (!block.diodes.empty()) mix["Diode"] = static_cast<int>(block.diodes.size());
        if (!block.mosfets.empty()) mix["MOSFET"] = static_cast<int>(block.mosfets.size());
        if (!block.bjts.empty()) mix["BJT"] = static_cast<int>(block.bjts.size());
        if (!block.jfets.empty()) mix["JFET"] = static_cast<int>(block.jfets.size());
        if (!block.vsources.empty()) mix["VSource"] = static_cast<int>(block.vsources.size());
        if (!block.isources.empty()) mix["ISource"] = static_cast<int>(block.isources.size());
        
        return mix;
    }
    
    /**
     * Determine solver type string from run metadata.
     */
    static QString getSolverType(const Simulation::SimulationRun& run) {
        // Check trust method for hints
        if (run.trustMethod.contains("PCG", Qt::CaseInsensitive)) return "PCG";
        if (run.trustMethod.contains("GMRES", Qt::CaseInsensitive)) return "GMRES";
        if (run.trustMethod.contains("LU", Qt::CaseInsensitive)) return "LU";
        return "Newton-Raphson"; // Default
    }
    
    /**
     * Extract failure information from a failed run.
     */
    static QVector<TraceFailure> extractFailures(const Simulation::SimulationRun& run) {
        QVector<TraceFailure> failures;
        
        TraceFailure failure;
        
        switch (run.status) {
            case Simulation::SimulationRun::Status::FAILED:
                failure.failureType = "Convergence Failed";
                failure.description = run.failureReason.isEmpty() 
                    ? "Simulation did not converge" 
                    : run.failureReason;
                break;
                
            case Simulation::SimulationRun::Status::CANCELLED:
                failure.failureType = "User Cancelled";
                failure.description = "Simulation was cancelled by user";
                break;
                
            default:
                failure.failureType = "Unknown";
                failure.description = "Unknown failure mode";
                break;
        }
        
        failure.timestamp = 0.0; // DC or start of transient
        
        // Add debug context
        if (run.trustGminUsed) {
            failure.debugContext["gminUsed"] = true;
        }
        if (run.trustSourceStepUsed) {
            failure.debugContext["sourceSteppingUsed"] = true;
        }
        failure.debugContext["iterations"] = run.iterations;
        failure.debugContext["maxResidual"] = run.trustMaxResidual;
        
        failures.append(failure);
        return failures;
    }
};

} // namespace AIAPI
