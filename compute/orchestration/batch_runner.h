#pragma once
#include <QString>
#include <QVector>
#include <QObject>
#include <functional>
#include "simulation_context.h"
#include "infrastructure/netlist_compiler.h"
#include "infrastructure/postproc_engine.h"

// Forward declare solver/worker to avoid circular dependency
// In reality we might need to inject a solver interface
namespace acutesim::compute::orchestration {

using acutesim::orchestration::SimulationContext;

struct SweepParameter {
    QString variableName;
    double start;
    double stop;
    double step; // or count if log
    bool isLog = false;
    
    QVector<double> generatePoints() const;
};

struct BatchResult {
    QString sweepParam;
    QVector<double> sweepValues;
    // Map of "v(out)" -> WaveFamily (a collection of waveforms)
    QMap<QString, acutesim::compute::postproc::WaveFamily> families;
};

class BatchRunner : public QObject {
    Q_OBJECT
public:
    explicit BatchRunner(QObject* parent = nullptr);
    
    // Configure the run
    void setContext(const SimulationContext& ctx);
    void setBaseNetlist(const TensorNetlist& netlist);
    void setSweep(const SweepParameter& sweep);
    
    // Execute synchronous (blocking) or async in future
    // Returns the aggregated results
    // We pass a callback to run the actual simulation step (Dependency Injection of Solver)
    // using SolverCallback = std::function<QJsonObject(std::shared_ptr<const CompiledTensorBlock>)>;
    // For V1 Parity, let's just make it virtual or take a std::function
    
    using SolverFn = std::function<QJsonObject(std::shared_ptr<const CompiledTensorBlock>)>;
    
    BatchResult run(SolverFn solver);

    // Signals for progress
signals:
    void progressChanged(int percent);
    void stepCompleted(int step, int total);

private:
    SimulationContext ctx_;
    TensorNetlist netlist_;
    SweepParameter sweep_;
};

} // namespace acutesim::compute::orchestration
