#include "local_worker_driver.h"
#include "batch_execution_engine.h"
#include "job_scheduler.h"
#include "veriloga_parser.h"
#include <iostream>
#include <chrono>

#include "../../../physics/circuitsim.h"
#include "../../../components/circuit.h"
#include "../../../compute/solvers/ac_solver.h"
#include "../../../compute/solvers/tf_analysis.h"
#include "../../../compute/solvers/pz_analysis.h"
#include "../../../compute/solvers/fourier_analysis.h"
#include "../../../compute/solvers/register_analyses.h"
#include "compute/analysis_registry.h"
#include "../../../compute/infrastructure/compiled_block.h"
#include "../../../compute/infrastructure/netlist_compiler.h"

#include <mutex>
#include <atomic>
#include <thread>
#include <queue>
#include <condition_variable>

namespace acutesim {
namespace compute {
namespace orchestration {

static double parseUnitValue(const std::string& str, double defaultVal = 0.0) {
    if (str.empty()) return defaultVal;

    // Simple unit parser (s, ms, us, ns, Hz, kHz, MHz, GHz)
    std::string s = str;
    double multiplier = 1.0;
    size_t suffixLen = 0;

    if (s.size() >= 3) {
        std::string last3 = s.substr(s.size() - 3);
        if (last3 == "GHz" || last3 == "ghz") { multiplier = 1e9; suffixLen = 3; }
        else if (last3 == "MHz" || last3 == "mhz") { multiplier = 1e6; suffixLen = 3; }
        else if (last3 == "kHz" || last3 == "khz") { multiplier = 1e3; suffixLen = 3; }
    }
    if (suffixLen == 0 && s.size() >= 2) {
        std::string last2 = s.substr(s.size() - 2);
        if (last2 == "Hz" || last2 == "hz") { multiplier = 1.0; suffixLen = 2; }
        else if (last2 == "ms") { multiplier = 1e-3; suffixLen = 2; }
        else if (last2 == "us") { multiplier = 1e-6; suffixLen = 2; }
        else if (last2 == "ns") { multiplier = 1e-9; suffixLen = 2; }
    }
    if (suffixLen == 0 && s.size() >= 1 && s.back() == 's') {
        multiplier = 1.0; suffixLen = 1;
    }

    if (suffixLen > 0) {
        try {
            double val = std::stod(s.substr(0, s.size() - suffixLen));
            return val * multiplier;
        } catch (...) {}
    }

    try { return std::stod(s); } catch (...) {}
    return defaultVal;
}

struct LocalWorkerDriver::Impl {
    LocalWorkerDriver* q_ptr;

    enum class JobType { SINGLE, BATCH, MONTE_CARLO };

    struct InternalJob {
        JobID id;
        JobType type;
        SimulationRequestDTO singleRequest;
        SimulationBatchRequestDTO batchRequest;
        MonteCarloConfigDTO mcConfig;  // Used for MONTE_CARLO jobs
    };

    BatchExecutionEngine batchEngine_;
    // Phase 4.3: Job scheduler for concurrent batch execution
    JobScheduler         jobScheduler_{0};  // 0 = hardware_concurrency
    std::atomic<JobID> nextJobId_{1};
    std::atomic<bool>  cancelRequested_{false};

    // Phase 4.5: New analysis helpers (operate on pre-built TensorNetlist)
    SimulationResponseDTO runTF(CircuitSim& sim,
                                const TensorNetlist& netlist,
                                const SimulationRequestDTO& req);
    SimulationResponseDTO runPZ(CircuitSim& sim,
                                const TensorNetlist& netlist,
                                const SimulationRequestDTO& req);
    SimulationResponseDTO runFourier(CircuitSim& sim,
                                     const TensorNetlist& netlist,
                                     const SimulationRequestDTO& req);

    // Build a TensorNetlist strictly from Verilog-A source.
    TensorNetlist buildNetlist(const SimulationSourceDTO& source) {
        if (source.format == NetlistFormatDTO::VERILOG_A && !source.payload.empty()) {
            auto r = parseVerilogA(source.payload);
            if (r.success) return r.netlist;
            
            std::cerr << "LocalWorkerDriver: Verilog-A Parse Error: " << r.errorMessage << std::endl;
        }
        
        // Return empty netlist if no valid Verilog-A source provided.
        // Legacy JSON path is now DISCONTINUED.
        return TensorNetlist();
    }

    std::unique_ptr<std::thread> workerThread_;
    std::mutex                   queueMutex_;
    std::condition_variable      queueCV_;
    std::queue<InternalJob>      jobQueue_;
    std::atomic<bool>            shutdown_{false};

    Impl(LocalWorkerDriver* q) : q_ptr(q) {}
    JobID submitJob(const SimulationRequestDTO& request);
    JobID submitBatchJob(const SimulationBatchRequestDTO& req);
    JobID submitStatisticalJob(const SimulationRequestDTO& baseReq, const MonteCarloConfigDTO& config);
    void cancelJob(JobID id);
    void workerLoop();
    void executeJob(const InternalJob& job);
    SimulationResponseDTO runDC(CircuitSim& sim, const TensorNetlist& netlist, const SimulationRequestDTO& req);
    SimulationResponseDTO runTransient(CircuitSim& sim, const TensorNetlist& netlist, const SimulationRequestDTO& req);
    SimulationResponseDTO runAC(CircuitSim& sim, const TensorNetlist& netlist, const SimulationRequestDTO& req);

};

JobID LocalWorkerDriver::Impl::submitJob(const SimulationRequestDTO& request) {
        // ── B1 UNIFICATION: Route all single jobs through batch engine ──
        // Wrap into a single-run batch to guarantee identical:
        //   - Cancellation logic
        //   - Progress reporting
        //   - Telemetry path
        //   - Determinism
        JobID id = nextJobId_.fetch_add(1);
        cancelRequested_.store(false);

        InternalJob job;
        job.id = id;
        job.type = JobType::SINGLE;
        job.singleRequest = request;

        // Build a single-run batch from the single request
        SimulationBatchRequestDTO singleRunBatch;
        singleRunBatch.source          = request.source;  // forward VA source
        singleRunBatch.analysis = request.analysis;
        singleRunBatch.parameters = request.parameters;
        singleRunBatch.sessionToken = request.sessionToken;

        // Single run with no overrides
        BatchRunDTO singleRun;
        singleRun.runIndex = 0;
        singleRunBatch.runs.push_back(singleRun);

        job.batchRequest = singleRunBatch;

#ifdef __EMSCRIPTEN__
        executeJob(job);
#else
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            jobQueue_.push(std::move(job));
        }
        queueCV_.notify_one();
#endif

        return id;
    }

    JobID LocalWorkerDriver::Impl::submitBatchJob(const SimulationBatchRequestDTO& req) {
        JobID id = nextJobId_.fetch_add(1);
        cancelRequested_.store(false);

        InternalJob job;
        job.id = id;
        job.type = JobType::BATCH;
        job.batchRequest = req;

#ifdef __EMSCRIPTEN__
        executeJob(job);
#else
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            jobQueue_.push(std::move(job));
        }
        queueCV_.notify_one();
#endif

        return id;
    }

JobID LocalWorkerDriver::Impl::submitStatisticalJob(const SimulationRequestDTO& baseReq, const MonteCarloConfigDTO& config) {
    JobID id = nextJobId_.fetch_add(1);
    cancelRequested_.store(false);

    InternalJob job;
    job.id = id;
    job.type = JobType::MONTE_CARLO;
    job.singleRequest = baseReq;
    job.mcConfig = config;

#ifdef __EMSCRIPTEN__
    executeJob(job);
#else
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        jobQueue_.push(std::move(job));
    }
    queueCV_.notify_one();
#endif

    return id;
}

void LocalWorkerDriver::Impl::cancelJob(JobID /*id*/) {
    cancelRequested_.store(true);
}

// ── Worker Thread Loop (Desktop Only) ───────────────────────────────────────

void LocalWorkerDriver::Impl::workerLoop() {
    while (!shutdown_.load()) {
        InternalJob job;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCV_.wait(lock, [this] {
                return !jobQueue_.empty() || shutdown_.load();
            });
            if (shutdown_.load()) break;
            job = std::move(jobQueue_.front());
            jobQueue_.pop();
        }
        executeJob(job);
    }
}

// ── Job Execution Dispatch ──────────────────────────────────────────────────

void LocalWorkerDriver::Impl::executeJob(const InternalJob& job) {
    // ── UNIFIED EXECUTION (B1) ──────────────────────────────────────────
    // ALL job types route through BatchExecutionEngine.
    // SINGLE jobs were wrapped into a single-run batch by submitJob().
    // This guarantees identical cancellation, progress, telemetry, and
    // determinism for every execution path.

    // Parse base netlist once; solver lambda captures it by reference.
    const SimulationSourceDTO& baseSource = (job.type == JobType::MONTE_CARLO)
        ? job.singleRequest.source
        : job.batchRequest.source;
    const std::unordered_map<std::string, std::string>& jobParams =
        (job.type == JobType::MONTE_CARLO)
        ? job.singleRequest.parameters
        : job.batchRequest.parameters;

    // Dispatch: strictly Verilog-A only.
    TensorNetlist baseNetlist = buildNetlist(baseSource);

    // Capture originating request metadata (topology hash + compute mode)
    const uint64_t baseTopologyHash = job.singleRequest.topologyHash;
    const int      baseComputeMode  = job.singleRequest.compute_mode;

    // RunSolver: DTO → internal override application → solver dispatch
    auto solverLambda = [this, &baseNetlist, &jobParams, baseTopologyHash, baseComputeMode](
        AnalysisTypeDTO analysis,
        const std::vector<ParameterVariationDTO>& overrides,
        const CornerParametersDTO& corner) -> SimulationResponseDTO
    {
        TensorNetlist runNetlist = baseNetlist;  // deep copy

        // Apply targeted property overrides
        for (const auto& ov : overrides) {
            for (auto& r : runNetlist.globalBlock.resistors)
                if (r.name == ov.componentName && ov.propertyName == "resistance")
                    r.resistance_ohms = ov.overrideValue;
            for (auto& c : runNetlist.globalBlock.capacitors)
                if (c.name == ov.componentName && ov.propertyName == "capacitance")
                    c.capacitance_farads = ov.overrideValue;
            for (auto& l : runNetlist.globalBlock.inductors)
                if (l.name == ov.componentName && ov.propertyName == "inductance")
                    l.inductance_henries = ov.overrideValue;
        }

        SimulationRequestDTO runReq;
        runReq.analysis      = analysis;
        runReq.corner        = corner;
        runReq.parameters    = jobParams;
        // Propagate topology hash and compute mode from the originating request
        runReq.topologyHash  = baseTopologyHash;
        runReq.compute_mode  = baseComputeMode;

        // Phase 4 Reconcile: each worker thread gets its own CircuitSim —
        // no shared numeric state across Monte Carlo parallel workers.
        CircuitSim sim;
        sim.execMode = static_cast<CircuitSim::ExecutionMode>(runReq.compute_mode);

        try {
            // Pure registry dispatch — zero switch statement.
            // All analysis types (DC, TRANSIENT, AC, TF, PZ, FOURIER, ...)
            // are handled by registered handlers. Adding a new analysis type
            // requires only a new handler + registerHandler() call.
            return acutesim::compute::AnalysisRegistry::instance()
                .dispatch(analysis, runReq, runNetlist, sim);
        } catch (const std::exception& ex) {
            SimulationResponseDTO e;
            e.success = false;
            e.errorMessage = std::string("Solver exception: ") + ex.what();
            return e;
        }
    };

    BatchExecutionEngine::ProgressFn progressFn =
        [this, jobId = job.id](uint32_t completed, uint32_t total) {
            if (q_ptr->onBatchProgress)
                q_ptr->onBatchProgress(jobId, completed, total);
        };

    SimulationBatchResponseDTO batchResp;
    if (job.type == JobType::MONTE_CARLO) {
        // Phase 4 Reconcile: pass jobScheduler_ to enable real parallel execution.
        // executeMonteCarlo routes to executeBatchParallel when scheduler != nullptr.
        batchResp = batchEngine_.executeMonteCarlo(
            job.singleRequest, job.mcConfig, solverLambda, progressFn, cancelRequested_,
            &jobScheduler_);
    } else {
        // Both SINGLE and BATCH go through executeBatch
        batchResp = batchEngine_.executeBatch(
            job.batchRequest, solverLambda, progressFn, cancelRequested_);
    }

    // Fire batch completion for all consumers
    if (q_ptr->onBatchCompleted) q_ptr->onBatchCompleted(job.id, batchResp);

    // For SINGLE jobs: unwrap first run result back to onJobCompleted
    // for backward compatibility with SimulationWorkspace
    if (job.type == JobType::SINGLE && q_ptr->onJobCompleted) {
        if (!batchResp.runResults.empty()) {
            q_ptr->onJobCompleted(job.id, batchResp.runResults[0]);
        } else {
            SimulationResponseDTO emptyResp;
            emptyResp.success = false;
            emptyResp.errorMessage = "No results from single-run batch";
            q_ptr->onJobCompleted(job.id, emptyResp);
        }
    }
}

// ── DC Analysis ─────────────────────────────────────────────────────────────

SimulationResponseDTO LocalWorkerDriver::Impl::runDC(
    CircuitSim& sim, const TensorNetlist& netlist, const SimulationRequestDTO& req)
{
    SimulationResponseDTO resp;
    resp.topologyHash = req.topologyHash;

    // Apply corner
    TensorNetlist runNetlist = netlist;  // Deep copy for corner application
    acutesim::physics::CornerConfig corner;
    corner.name = req.corner.processCorner;
    corner.temperatureC = req.corner.temperature;
    corner.voltageScale = req.corner.supplyVoltage / 1.8; // Assume 1.8V nominal for now
    sim.applyCorner(runNetlist, corner);

    auto t0 = std::chrono::steady_clock::now();
    SolverStep result = sim.solveDC(runNetlist);
    auto t1 = std::chrono::steady_clock::now();

    if (result.stats.converged) {
        resp.success = true;
        resp.dcOperatingPoint = result.nodeVoltages;

        // Populate node voltages map (single DC point)
        for (int i = 0; i < (int)result.nodeVoltages.size(); ++i) {
            resp.nodeVoltages[i] = {result.nodeVoltages[i]};
        }

        // Populate probe values
        for (const auto& probe : runNetlist.globalBlock.probes) {
            double v_pos = (probe.nodePositive > 0 &&
                           probe.nodePositive <= (int)result.nodeVoltages.size())
                          ? result.nodeVoltages[probe.nodePositive - 1] : 0.0;
            double v_neg = (probe.nodeNegative > 0 &&
                           probe.nodeNegative <= (int)result.nodeVoltages.size())
                          ? result.nodeVoltages[probe.nodeNegative - 1] : 0.0;
            resp.probeValues[probe.name] = {v_pos - v_neg};
        }

        // Convergence telemetry
        resp.convergence.iterations         = result.stats.iterations;
        resp.convergence.maxResidual        = result.stats.residual;
        resp.convergence.rmsResidual        = result.stats.rmsResidual;
        resp.convergence.method             = result.stats.method;
        resp.convergence.gminSteppingUsed   = result.stats.gminSteppingUsed;
        resp.convergence.sourceSteppingUsed = result.stats.sourceSteppingUsed;
        resp.convergence.solveTimeMs        = std::chrono::duration<double, std::milli>(t1 - t0).count();
        // Phase 2.9: Numerical Intelligence
        resp.convergence.rankDeficient      = result.stats.rankDeficient;
        resp.convergence.rankDeficientNode  = result.stats.rankDeficientRow;
        resp.convergence.smallestPivot      = result.stats.smallestPivot;
        resp.convergence.stagnationDetected = result.stats.stagnationDetected;
        resp.convergence.divergenceDetected = result.stats.divergenceDetected;
        resp.convergence.worstNodeIndex     = result.stats.worstNodeIndex;
        resp.convergence.worstNodeVoltage   = result.stats.worstNodeVoltage;
        resp.convergence.gminApplied        = result.stats.regularization.gminValue;
        resp.convergence.gminInjections     = result.stats.regularization.injections;
    } else {
        resp.success = false;
        resp.errorMessage = result.stats.error_detail.empty()
            ? "DC solver diverged"
            : result.stats.error_detail;
    }

    return resp;
}

// ── Transient Analysis ──────────────────────────────────────────────────────

SimulationResponseDTO LocalWorkerDriver::Impl::runTransient(
    CircuitSim& sim, const TensorNetlist& netlist, const SimulationRequestDTO& req)
{
    SimulationResponseDTO resp;
    resp.topologyHash = req.topologyHash;

    TensorNetlist runNetlist = netlist;
    acutesim::physics::CornerConfig corner;
    corner.name = req.corner.processCorner;
    corner.temperatureC = req.corner.temperature;
    corner.voltageScale = req.corner.supplyVoltage / 1.8;
    sim.applyCorner(runNetlist, corner);

    // Extract parameters
    double stopTime = 1e-3; // default 1ms
    auto it = req.parameters.find("stopTime");
    if (it != req.parameters.end()) {
        stopTime = parseUnitValue(it->second, 1e-3);
    }

    double currentTime = 0.0;
    double timeStep = 1e-5;
    int stepCount = 0;
    int totalIterations = 0;
    double maxResidual = 0.0;
    bool failed = false;

    while (currentTime < stopTime && !cancelRequested_.load()) {
        SolverStep result = sim.stepTransient(runNetlist, timeStep, currentTime);
        currentTime = result.time;
        stepCount++;
        totalIterations += result.stats.iterations;
        if (result.stats.residual > maxResidual) {
            maxResidual = result.stats.residual;
        }

        if (!result.stats.converged) {
            resp.success = false;
            resp.errorMessage = "Transient failed at t=" + std::to_string(currentTime) +
                "s: " + result.stats.error_detail;
            failed = true;
            break;
        }

        // Accumulate time vector
        resp.timeVector.push_back(currentTime);

        // Accumulate node voltages
        for (int i = 0; i < (int)result.nodeVoltages.size(); ++i) {
            resp.nodeVoltages[i].push_back(result.nodeVoltages[i]);
        }

        // Accumulate probe values
        for (const auto& probe : runNetlist.globalBlock.probes) {
            double v_pos = (probe.nodePositive > 0 &&
                           probe.nodePositive <= (int)result.nodeVoltages.size())
                          ? result.nodeVoltages[probe.nodePositive - 1] : 0.0;
            double v_neg = (probe.nodeNegative > 0 &&
                           probe.nodeNegative <= (int)result.nodeVoltages.size())
                          ? result.nodeVoltages[probe.nodeNegative - 1] : 0.0;
            resp.probeValues[probe.name].push_back(v_pos - v_neg);
        }

        // Progress callback
        if (q_ptr->onJobProgress && stepCount % 50 == 0) {
            float pct = static_cast<float>(currentTime / stopTime);
            q_ptr->onJobProgress(0, pct, "t = " + std::to_string(currentTime * 1e6) + " us");
        }
    }

    if (!failed) {
        resp.success = true;
        resp.convergence.iterations       = totalIterations;
        resp.convergence.maxResidual      = maxResidual;
        resp.convergence.method           = "Newton-Raphson";
        resp.convergence.gminSteppingUsed = false;
        resp.convergence.sourceSteppingUsed = false;
    }

    return resp;
}

// ── AC Analysis ─────────────────────────────────────────────────────────────

SimulationResponseDTO LocalWorkerDriver::Impl::runAC(
    CircuitSim& sim, const TensorNetlist& netlist, const SimulationRequestDTO& req)
{
    SimulationResponseDTO resp;
    resp.topologyHash = req.topologyHash;

    // Apply corner for AC too
    TensorNetlist runNetlist = netlist;
    acutesim::physics::CornerConfig corner;
    corner.name = req.corner.processCorner;
    corner.temperatureC = req.corner.temperature;
    corner.voltageScale = req.corner.supplyVoltage / 1.8;
    sim.applyCorner(runNetlist, corner);

    // 1. DC operating point first
    SolverStep dcResult = sim.solveDC(runNetlist);
    if (!dcResult.stats.converged) {
        resp.success = false;
        resp.errorMessage = "AC: DC operating point failed: " + dcResult.stats.error_detail;
        return resp;
    }

    // 2. Extract frequency parameters
    double fStart = 1.0, fStop = 1e9;
    int points = 200;

    auto itS = req.parameters.find("fStart");
    if (itS != req.parameters.end()) fStart = parseUnitValue(itS->second, 1.0);
    auto itE = req.parameters.find("fStop");
    if (itE != req.parameters.end()) fStop = parseUnitValue(itE->second, 1e9);
    auto itP = req.parameters.find("points");
    if (itP != req.parameters.end()) {
        try { points = std::stoi(itP->second); } catch (...) {}
    }

    // 3. Run AC sweep
    ACSolver::ACResult acResult = ACSolver::solveAC(
        netlist, dcResult.nodeVoltages, fStart, fStop, points);

    // 4. Pack into DTO
    resp.success = true;
    resp.timeVector = acResult.frequencies; // frequencies as the "x-axis"

    int n = netlist.numGlobalNodes;
    for (int node = 0; node < n; ++node) {
        std::vector<double> magVec;
        magVec.reserve(points);
        for (int fi = 0; fi < points; ++fi) {
            if (fi < (int)acResult.nodeVoltages.size() &&
                node < (int)acResult.nodeVoltages[fi].size()) {
                magVec.push_back(std::abs(acResult.nodeVoltages[fi][node]));
            }
        }
        resp.nodeVoltages[node] = std::move(magVec);
    }

    resp.convergence.iterations = dcResult.stats.iterations;
    resp.convergence.maxResidual = dcResult.stats.residual;
    resp.convergence.method = "AC Small-Signal";

    return resp;
}

// ── Phase 4.5: Transfer Function Analysis ───────────────────────────────────

SimulationResponseDTO LocalWorkerDriver::Impl::runTF(
    CircuitSim& sim, const TensorNetlist& netlist, const SimulationRequestDTO& req)
{
    // 1. DC operating point required for linearisation
    TensorNetlist runNetlist = netlist;
    acutesim::physics::CornerConfig corner;
    corner.name         = req.corner.processCorner;
    corner.temperatureC = req.corner.temperature;
    corner.voltageScale = req.corner.supplyVoltage / 1.8;
    sim.applyCorner(runNetlist, corner);

    SolverStep dcResult = sim.solveDC(runNetlist);
    if (!dcResult.stats.converged) {
        SimulationResponseDTO e;
        e.success      = false;
        e.errorMessage = "TF: DC operating point failed: " + dcResult.stats.error_detail;
        return e;
    }

    return acutesim::compute::solvers::runTFAnalysis(runNetlist, dcResult.nodeVoltages, req);
}

// ── Phase 4.5: Pole-Zero Analysis ───────────────────────────────────────────

SimulationResponseDTO LocalWorkerDriver::Impl::runPZ(
    CircuitSim& sim, const TensorNetlist& netlist, const SimulationRequestDTO& req)
{
    TensorNetlist runNetlist = netlist;
    acutesim::physics::CornerConfig corner;
    corner.name         = req.corner.processCorner;
    corner.temperatureC = req.corner.temperature;
    corner.voltageScale = req.corner.supplyVoltage / 1.8;
    sim.applyCorner(runNetlist, corner);

    SolverStep dcResult = sim.solveDC(runNetlist);
    if (!dcResult.stats.converged) {
        SimulationResponseDTO e;
        e.success      = false;
        e.errorMessage = "PZ: DC operating point failed: " + dcResult.stats.error_detail;
        return e;
    }

    return acutesim::compute::solvers::runPZAnalysis(runNetlist, dcResult.nodeVoltages, req);
}

// ── Phase 4.5: Fourier / THD Analysis ───────────────────────────────────────

SimulationResponseDTO LocalWorkerDriver::Impl::runFourier(
    CircuitSim& sim, const TensorNetlist& netlist, const SimulationRequestDTO& req)
{
    // Fourier post-processes a transient waveform.
    // Run a transient first, then extract the probe node waveform.
    SimulationResponseDTO tranResp = runTransient(sim, netlist, req);
    if (!tranResp.success) {
        tranResp.errorMessage = "FOURIER: transient failed: " + tranResp.errorMessage;
        return tranResp;
    }

    // Pick output node (default = last node)
    int outNode = netlist.numGlobalNodes;
    auto it = req.parameters.find("outputNode");
    if (it != req.parameters.end()) {
        try { outNode = std::stoi(it->second); } catch (...) {}
    }
    outNode = std::max(1, std::min(outNode, netlist.numGlobalNodes));

    std::vector<double> waveform;
    auto vit = tranResp.nodeVoltages.find(outNode - 1);
    if (vit != tranResp.nodeVoltages.end())
        waveform = vit->second;

    SimulationResponseDTO fResp = acutesim::compute::solvers::runFourierAnalysis(
        netlist, tranResp.timeVector, waveform, req);

    // Preserve transient data in response alongside Fourier result
    fResp.timeVector    = tranResp.timeVector;
    fResp.nodeVoltages  = tranResp.nodeVoltages;
    fResp.probeValues   = tranResp.probeValues;
    fResp.topologyHash  = req.topologyHash;
    return fResp;
}

LocalWorkerDriver::LocalWorkerDriver() : impl_(std::make_unique<Impl>(this)) {
    // Phase 4 Reconcile: register all analysis handlers at construction time.
    // This is idempotent and safe to call multiple times.
    // Ensures TF / PZ / FOURIER handlers are present regardless of linker
    // dead-stripping of static-init registrations in static libs.
    acutesim::compute::solvers::registerAllAnalyses();

    // DC / TRANSIENT / AC are handled by the Impl's run* helpers.
    // Register them directly here so the registry is the single dispatch point.
    using namespace acutesim::compute;
    using namespace acutesim::compute::orchestration;
    auto& reg = AnalysisRegistry::instance();

    // Capture impl_ pointer for lambdas (safe: lambdas are registered once and
    // the registry is cleared only when the driver is destroyed).
    Impl* p = impl_.get();
    reg.registerHandler(AnalysisTypeDTO::DC,        [p](const SimulationRequestDTO& req, const TensorNetlist& nl, CircuitSim& sim){ return p->runDC(sim, nl, req); });
    reg.registerHandler(AnalysisTypeDTO::TRANSIENT,  [p](const SimulationRequestDTO& req, const TensorNetlist& nl, CircuitSim& sim){ return p->runTransient(sim, nl, req); });
    reg.registerHandler(AnalysisTypeDTO::AC,         [p](const SimulationRequestDTO& req, const TensorNetlist& nl, CircuitSim& sim){ return p->runAC(sim, nl, req); });
    reg.registerHandler(AnalysisTypeDTO::NOISE,      [p](const SimulationRequestDTO& req, const TensorNetlist& nl, CircuitSim& sim){ return p->runAC(sim, nl, req); }); // AC path for now
    reg.registerHandler(AnalysisTypeDTO::SENSITIVITY,[p](const SimulationRequestDTO& req, const TensorNetlist& nl, CircuitSim& sim){ return p->runDC(sim, nl, req); }); // DC path for sensitivity
    reg.registerHandler(AnalysisTypeDTO::MONTE_CARLO,[p](const SimulationRequestDTO& req, const TensorNetlist& nl, CircuitSim& sim){ return p->runDC(sim, nl, req); }); // base run

#ifndef __EMSCRIPTEN__
    impl_->shutdown_.store(false);
    impl_->workerThread_ = std::make_unique<std::thread>(&LocalWorkerDriver::Impl::workerLoop, impl_.get());
#endif
}

LocalWorkerDriver::~LocalWorkerDriver() {
#ifndef __EMSCRIPTEN__
    impl_->shutdown_.store(true);
    impl_->queueCV_.notify_all();
    if (impl_->workerThread_ && impl_->workerThread_->joinable()) {
        impl_->workerThread_->join();
    }
#endif
}

JobID LocalWorkerDriver::submitJob(const SimulationRequestDTO& request) { return impl_->submitJob(request); }
JobID LocalWorkerDriver::submitBatchJob(const SimulationBatchRequestDTO& req) { return impl_->submitBatchJob(req); }
JobID LocalWorkerDriver::submitStatisticalJob(const SimulationRequestDTO& baseReq, const MonteCarloConfigDTO& config) { return impl_->submitStatisticalJob(baseReq, config); }
void LocalWorkerDriver::cancelJob(JobID id) { impl_->cancelJob(id); }

// Phase 4 Reconcile: supportedAnalyses() reads from the live registry.
// Adding a new analysis requires only registering a handler — never editing this file.
std::vector<AnalysisTypeDTO> LocalWorkerDriver::supportedAnalyses() const {
    return acutesim::compute::AnalysisRegistry::instance().registeredTypes();
}

// ── Terminal Command Parser ─────────────────────────────────────────────────
// Parses high-level commands and reuses existing batch execution paths.
// Supported commands:
//   run dc                 → submitJob(DC)
//   run tran <stopTime>    → submitJob(TRANSIENT, stopTime)
//   run ac                 → submitJob(AC)
//   sweep <comp> <lo> <hi> <steps>   → submitBatchJob(...)
//   mc <samples> seed=<N>            → submitStatisticalJob(...)
//   help                   → returns command list
//   status                 → returns driver status

static std::vector<std::string> tokenize(const std::string& s) {
    std::vector<std::string> tokens;
    std::string tok;
    for (char c : s) {
        if (c == ' ' || c == '\t') {
            if (!tok.empty()) { tokens.push_back(tok); tok.clear(); }
        } else {
            tok += c;
        }
    }
    if (!tok.empty()) tokens.push_back(tok);
    return tokens;
}

TerminalResponseDTO LocalWorkerDriver::executeCommand(const TerminalCommandDTO& cmd) {
    TerminalResponseDTO resp;
    auto tokens = tokenize(cmd.rawCommand);

    if (tokens.empty()) {
        resp.success = true;
        resp.output = "";
        return resp;
    }

    const auto& verb = tokens[0];

    // ── help ──
    if (verb == "help") {
        resp.success = true;
        resp.output =
            "Available commands:\n"
            "  run dc                        — Run DC operating point analysis\n"
            "  run tran <stopTime>           — Run transient analysis (e.g. run tran 1ms)\n"
            "  run ac                        — Run AC frequency analysis\n"
            "  sweep <comp> <lo> <hi> <n>    — Parametric sweep (e.g. sweep R1 1k 10k 10)\n"
            "  mc <samples> [seed=<N>]       — Monte Carlo analysis\n"
            "  status                        — Show driver status\n"
            "  help                          — Show this message\n";
        return resp;
    }

    // ── status ──
    if (verb == "status") {
        resp.success = true;
        resp.output = "LocalWorkerDriver: operational, concurrent=" +
                      std::string(supportsConcurrentJobs() ? "yes" : "no");
        return resp;
    }

    // ── run <type> [params] ──
    if (verb == "run") {
        if (tokens.size() < 2) {
            resp.success = false;
            resp.errorMessage = "Usage: run <dc|tran|ac> [params]";
            return resp;
        }

        SimulationRequestDTO req;
        req.sessionToken = cmd.sessionToken;

        const auto& analysisStr = tokens[1];
        if (analysisStr == "dc") {
            req.analysis = AnalysisTypeDTO::DC;
        } else if (analysisStr == "tran") {
            req.analysis = AnalysisTypeDTO::TRANSIENT;
            if (tokens.size() >= 3) {
                req.parameters["stopTime"] = std::to_string(parseUnitValue(tokens[2], 0.001));
            } else {
                req.parameters["stopTime"] = "0.001"; // default 1ms
            }
        } else if (analysisStr == "ac") {
            req.analysis = AnalysisTypeDTO::AC;
        } else {
            resp.success = false;
            resp.errorMessage = "Unknown analysis type: " + analysisStr + ". Use: dc, tran, ac";
            return resp;
        }

        // Note: netlistJson must be set by caller before routing through terminal
        JobID jid = submitJob(req);
        resp.success = true;
        resp.output = "Submitted " + analysisStr + " job (ID=" + std::to_string(jid) + ")";
        return resp;
    }

    // ── sweep <component> <lo> <hi> <steps> ──
    if (verb == "sweep") {
        if (tokens.size() < 5) {
            resp.success = false;
            resp.errorMessage = "Usage: sweep <component> <lo> <hi> <steps>";
            return resp;
        }

        std::string comp = tokens[1];
        double lo = parseUnitValue(tokens[2]);
        double hi = parseUnitValue(tokens[3]);
        int steps = std::stoi(tokens[4]);

        if (steps < 1) { steps = 1; }

        SimulationBatchRequestDTO batchReq;
        batchReq.sessionToken = cmd.sessionToken;
        batchReq.analysis = AnalysisTypeDTO::DC;

        for (int i = 0; i < steps; ++i) {
            BatchRunDTO run;
            run.runIndex = i;
            ParameterVariationDTO var;
            var.componentName = comp;
            var.propertyName = "resistance"; // default
            var.overrideValue = lo + (hi - lo) * i / (steps > 1 ? steps - 1 : 1);
            run.overrides.push_back(var);
            batchReq.runs.push_back(run);
        }

        JobID jid = submitBatchJob(batchReq);
        resp.success = true;
        resp.output = "Submitted " + std::to_string(steps) + "-step sweep on " + comp +
                      " [" + tokens[2] + "→" + tokens[3] + "] (ID=" + std::to_string(jid) + ")";
        return resp;
    }

    // ── mc <samples> [seed=<N>] ──
    if (verb == "mc") {
        if (tokens.size() < 2) {
            resp.success = false;
            resp.errorMessage = "Usage: mc <samples> [seed=<N>]";
            return resp;
        }

        SimulationRequestDTO baseReq;
        baseReq.sessionToken = cmd.sessionToken;
        baseReq.analysis = AnalysisTypeDTO::DC;

        MonteCarloConfigDTO mcConfig;
        mcConfig.sampleCount = static_cast<uint32_t>(std::stoi(tokens[1]));
        mcConfig.seed = 0; // random seed by default

        // Parse optional seed=N
        for (size_t i = 2; i < tokens.size(); ++i) {
            if (tokens[i].substr(0, 5) == "seed=") {
                mcConfig.seed = static_cast<uint64_t>(std::stoull(tokens[i].substr(5)));
            }
        }

        JobID jid = submitStatisticalJob(baseReq, mcConfig);
        resp.success = true;
        resp.output = "Submitted Monte Carlo (" + tokens[1] + " samples, seed=" +
                      std::to_string(mcConfig.seed) + ") (ID=" + std::to_string(jid) + ")";
        return resp;
    }

    resp.success = false;
    resp.errorMessage = "Unknown command: '" + verb + "'. Type 'help' for available commands.";
    return resp;
}


} // namespace orchestration
} // namespace compute
} // namespace acutesim
