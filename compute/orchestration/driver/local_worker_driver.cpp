#include "local_worker_driver.h"
#include "batch_execution_engine.h"
#include "job_scheduler.h"
#include "veriloga_parser.h"
#include <iostream>
#include <chrono>

#include "acutesim_engine/physics/circuitsim.h"
#include "acutesim_engine/netlist/circuit.h"
#include "acutesim_engine/solvers/tf_analysis.h"
#include "acutesim_engine/solvers/fourier_analysis.h"
#include "acutesim_engine/solvers/register_analyses.h"
#include "acutesim_engine/solvers/analysis_registry.h"

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

    // Build a TensorNetlist strictly from Verilog-A source.
    // Fails closed: any other format, or a parse error, sets `error` (it used to
    // return an empty netlist that then "simulated" successfully).
    TensorNetlist buildNetlist(const SimulationSourceDTO& source, std::string& error) {
        if (source.format != NetlistFormatDTO::VERILOG_A || source.payload.empty()) {
            error = "Source must be non-empty Verilog-A (the JSON transport was removed).";
            return {};
        }
        auto r = parseVerilogA(source.payload);
        if (!r.success) error = "Verilog-A parse error: " + r.errorDetail;
        return r.netlist;
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
    SimulationResponseDTO runNoise(CircuitSim& sim, const TensorNetlist& netlist, const SimulationRequestDTO& req);

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
    std::string sourceError;
    TensorNetlist baseNetlist = buildNetlist(baseSource, sourceError);

    // Capture originating request metadata (topology hash + compute mode)
    const uint64_t baseTopologyHash = job.singleRequest.topologyHash;
    const std::string baseSolverMode = job.singleRequest.solverMode;

    // RunSolver: DTO → internal override application → solver dispatch
    auto solverLambda = [this, &baseNetlist, &sourceError, &jobParams, baseTopologyHash, baseSolverMode](
        AnalysisTypeDTO analysis,
        const std::vector<ParameterVariationDTO>& overrides,
        const CornerParametersDTO& corner) -> SimulationResponseDTO
    {
        if (!sourceError.empty()) {
            SimulationResponseDTO e;
            e.errorDetail = sourceError;
            return e;
        }
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
        runReq.solverMode    = baseSolverMode;

        // Phase 4 Reconcile: each worker thread gets its own CircuitSim —
        // no shared numeric state across Monte Carlo parallel workers.
        CircuitSim sim;
        sim.execMode = (runReq.solverMode == "GPU") ? CircuitSim::ExecutionMode::GPU : CircuitSim::ExecutionMode::TENSOR_SOA_SIMD;

        try {
            // Pure registry dispatch — zero switch statement.
            // All analysis types (DC, TRANSIENT, AC, TF, FOURIER, ...)
            // are handled by registered handlers.
            return acutesim::compute::AnalysisRegistry::instance()
                .dispatch(analysis, runReq, runNetlist, sim);
        } catch (const std::exception& ex) {
            SimulationResponseDTO e;
            e.success = false;
            e.errorDetail = std::string("Solver exception: ") + ex.what();
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
            emptyResp.errorDetail = "No results from single-run batch";
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
            resp.probeValues[probe.name].push_back(v_pos - v_neg);
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
        resp.errorDetail = result.stats.error_detail.empty()
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

    double timeStep = stopTime / 100.0;
    if (auto h = req.parameters.find("stepHint"); h != req.parameters.end())
        timeStep = parseUnitValue(h->second, timeStep);
    // Batch runs carry only string parameters, so UIC comes as "uic" ("1"/"true")
    const auto u = req.parameters.find("uic");
    const bool uic = u != req.parameters.end() && (u->second == "1" || u->second == "true");

    int stepCount = 0, totalIterations = 0;
    double maxResidual = 0.0;
    const std::string err = sim.runTransient(
        runNetlist, timeStep, stopTime, uic, [&](const SolverStep& result) {
            if (cancelRequested_.load()) return false;
            ++stepCount;
            totalIterations += result.stats.iterations;
            maxResidual = std::max(maxResidual, result.stats.residual);
            resp.timeVector.push_back(result.time);
            for (int i = 0; i < (int)result.nodeVoltages.size(); ++i)
                resp.nodeVoltages[i].push_back(result.nodeVoltages[i]);
            for (const auto& probe : runNetlist.globalBlock.probes) {
                double v_pos = (probe.nodePositive > 0 &&
                               probe.nodePositive <= (int)result.nodeVoltages.size())
                              ? result.nodeVoltages[probe.nodePositive - 1] : 0.0;
                double v_neg = (probe.nodeNegative > 0 &&
                               probe.nodeNegative <= (int)result.nodeVoltages.size())
                              ? result.nodeVoltages[probe.nodeNegative - 1] : 0.0;
                resp.probeValues[probe.name].push_back(v_pos - v_neg);
            }
            if (q_ptr->onJobProgress && stepCount % 50 == 0) {
                float pct = static_cast<float>(result.time / stopTime);
                q_ptr->onJobProgress(0, pct, "t = " + std::to_string(result.time * 1e6) + " us");
            }
            return true;
        });

    resp.success = err.empty();
    resp.errorDetail = err;
    if (resp.success) {
        resp.convergence.iterations  = totalIterations;
        resp.convergence.maxResidual = maxResidual;
        resp.convergence.method      = "Newton-Raphson";
    }
    return resp;
}

// ── AC Analysis ─────────────────────────────────────────────────────────────

// AC/noise sweep from fStart, fStop, points, sweepType (dec/oct: points per
// decade/octave; lin/log: total points). Unknown sweep types fall back to log.
static std::vector<double> sweepFrequencies(const SimulationRequestDTO& req) {
    auto get = [&](const char* key, const char* def) {
        auto it = req.parameters.find(key);
        return it == req.parameters.end() ? std::string(def) : it->second;
    };
    std::string sweep = get("sweepType", "log");
    if (sweep != "dec" && sweep != "oct" && sweep != "lin") sweep = "log";
    return CircuitSim::acFrequencies(sweep, static_cast<int>(parseUnitValue(get("points", "200"), 200)),
                                     parseUnitValue(get("fStart", "1"), 1.0),
                                     parseUnitValue(get("fStop", "1e9"), 1e9));
}

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

    SolverStep dcResult = sim.solveDC(runNetlist);
    if (!dcResult.stats.converged) {
        resp.errorDetail = "AC: DC operating point failed: " + dcResult.stats.error_detail;
        return resp;
    }
    const auto freqs = sweepFrequencies(req);
    const auto X = sim.solveAC(runNetlist, freqs);  // excitation: the sources' AC magnitude/phase

    resp.success = X.size() == freqs.size() && !freqs.empty();
    if (!resp.success) resp.errorDetail = "AC: invalid frequency range";
    resp.timeVector = freqs;  // frequencies as the "x-axis"
    for (int node = 0; node < runNetlist.numGlobalNodes; ++node)
        for (const auto& x : X) resp.nodeVoltages[node].push_back(std::abs(x[node]));
    resp.convergence.iterations = dcResult.stats.iterations;
    resp.convergence.maxResidual = dcResult.stats.residual;
    resp.convergence.method = "AC Small-Signal";

    return resp;
}

// ── Noise Analysis ─────────────────────────────────────────────────────────
// Parameters: outputNode (index), refNode (default 0), inputSource (V/I name),
// fStart, fStop, points, sweepType (see sweepFrequencies).
SimulationResponseDTO LocalWorkerDriver::Impl::runNoise(
    CircuitSim& sim, const TensorNetlist& netlist, const SimulationRequestDTO& req)
{
    SimulationResponseDTO resp;
    resp.topologyHash = req.topologyHash;
    TensorNetlist runNetlist = netlist;
    auto num = [&](const char* key, double def) {
        auto it = req.parameters.find(key);
        return it == req.parameters.end() ? def : parseUnitValue(it->second, def);
    };
    auto src = req.parameters.find("inputSource");
    const auto freqs = sweepFrequencies(req);
    if (src == req.parameters.end() || freqs.empty()) {
        resp.errorDetail = "Noise needs inputSource, outputNode and a frequency range.";
        return resp;
    }
    SolverStep dc = sim.solveDC(runNetlist);
    if (!dc.stats.converged) {
        resp.errorDetail = "Noise: DC operating point failed: " + dc.stats.error_detail;
        return resp;
    }
    auto nz = sim.solveNoise(runNetlist, freqs, static_cast<int>(num("outputNode", 1)),
                             static_cast<int>(num("refNode", 0)), src->second);
    resp.errorDetail = nz.error;
    resp.success = resp.converged = nz.error.empty();
    resp.timeVector = freqs;
    resp.extended.hasNoise = resp.success;
    resp.extended.noise = {freqs, std::move(nz.onoise), std::move(nz.inoise), nz.onoiseTotal, nz.inoiseTotal};
    return resp;
}

LocalWorkerDriver::LocalWorkerDriver() : impl_(std::make_unique<Impl>(this)) {
    // Phase 4 Reconcile: register all analysis handlers at construction time.
    // This is idempotent and safe to call multiple times.
    // Ensures TF / FOURIER handlers are present regardless of linker
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
    reg.registerHandler(AnalysisTypeDTO::NOISE,      [p](const SimulationRequestDTO& req, const TensorNetlist& nl, CircuitSim& sim){ return p->runNoise(sim, nl, req); });
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
            resp.errorDetail = "Usage: run <dc|tran|ac> [params]";
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
            resp.errorDetail = "Unknown analysis type: " + analysisStr + ". Use: dc, tran, ac";
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
            resp.errorDetail = "Usage: sweep <component> <lo> <hi> <steps>";
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
            resp.errorDetail = "Usage: mc <samples> [seed=<N>]";
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
    resp.errorDetail = "Unknown command: '" + verb + "'. Type 'help' for available commands.";
    return resp;
}


} // namespace orchestration
} // namespace compute
} // namespace acutesim
