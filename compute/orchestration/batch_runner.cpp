#include "batch_runner.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QtMath>
#include <QDebug>

namespace acutesim::compute::orchestration {

QVector<double> SweepParameter::generatePoints() const {
    QVector<double> pts;
    if (step == 0) return pts;
    
    if (isLog) {
        // Log sweep: start * 10^(k/points_per_dec)? 
        // Or assume step is number of points?
        // Let's assume 'step' is total points for now if log?
        // Or specific log implementation. 
        // For parity with simple linear sweep:
        int points = static_cast<int>(step); 
        if (points < 2) points = 2;
        double logStart = std::log10(start);
        double logStop = std::log10(stop);
        double d = (logStop - logStart) / (points - 1);
        for(int i=0; i<points; ++i) {
            pts.append(std::pow(10.0, logStart + i*d));
        }
    } else {
        // Linear sweep
        // Check direction
        if (start < stop && step > 0) {
            for (double v = start; v <= stop + 1e-9; v += step) pts.append(v);
        } else if (start > stop && step < 0) {
            for (double v = start; v >= stop - 1e-9; v += step) pts.append(v);
        } else {
            // Single point
            pts.append(start);
        }
    }
    return pts;
}

BatchRunner::BatchRunner(QObject* parent) : QObject(parent) {}

void BatchRunner::setContext(const SimulationContext& ctx) {
    ctx_ = ctx;
}

void BatchRunner::setBaseNetlist(const TensorNetlist& netlist) {
    netlist_ = netlist;
}

void BatchRunner::setSweep(const SweepParameter& sweep) {
    sweep_ = sweep;
}

BatchResult BatchRunner::run(SolverFn solver) {
    BatchResult batchRes;
    batchRes.sweepParam = sweep_.variableName;
    batchRes.sweepValues = sweep_.generatePoints();
    
    int total = batchRes.sweepValues.size();
    if (total == 0) return batchRes;
    
    // Temp storage: Map<NodeName, Vector<Double>>
    QMap<QString, QVector<double>> scalarResults;
    
    for (int i = 0; i < total; ++i) {
        double val = batchRes.sweepValues[i];
        
        // 1. Update Context (Overwrite expression to numeric string)
        ctx_.variableExprs[sweep_.variableName] = QString::number(val);
        ctx_.resolveVariables(); // Re-resolve dependents
        
        // 2. Compile
        auto block = NetlistCompiler::compile(netlist_, ctx_);
        
        // 3. Run Solver
        QJsonObject stepRes = solver(block);
        
        // 4. Aggregate
        QString type = stepRes.value("type").toString();
        
        if (type == "DC") {
            // Voltages
            QJsonArray volts = stepRes.value("voltages").toArray();
            for (int n = 0; n < volts.size(); ++n) {
                QString key = QString("v(node%1)").arg(n+1);
                scalarResults[key].append(volts[n].toDouble());
            }
            // Probes
            QJsonObject probes = stepRes.value("probes").toObject();
            for (auto k : probes.keys()) {
                QString key = QString("v(\"%1\")").arg(k);
                scalarResults[key].append(probes.value(k).toDouble());
            }
        }
        
        emit stepCompleted(i + 1, total);
        emit progressChanged((i + 1) * 100 / total);
    }
    
    // Convert collected scalars to families
    for (auto k : scalarResults.keys()) {
        acutesim::compute::postproc::Waveform w;
        w.name = k;
        w.unit = "V";
        w.time = batchRes.sweepValues; // X-axis is the sweep parameter
        w.values = scalarResults[k];
        
        acutesim::compute::postproc::WaveFamily fam;
        fam.sweepParam = sweep_.variableName;
        fam.sweepValues = batchRes.sweepValues;
        fam.members.append(w); // Single curve for 1D sweep
        
        batchRes.families.insert(k, fam);
    }
    
    return batchRes;
}

} // namespace acutesim::compute::orchestration
