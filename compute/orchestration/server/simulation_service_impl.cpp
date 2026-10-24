#include <memory>
#include <cstdint>
#include "simulation_service_impl.h"
#include "../proto_mapper.h"
#include <condition_variable>

namespace acutesim::compute::orchestration {

// One write at a time: gRPC allows no concurrent Write on a stream, and the driver's callbacks run on its
// worker threads (Monte Carlo runs in parallel: GRPC_CALL_ERROR_TOO_MANY_OPERATIONS, then an abort).
// After the final message nothing is written: the handler has returned and the stream is gone.
namespace {
struct StreamGuard {
    grpc::ServerWriter<simulation::SimulationEvent>* w;
    std::mutex m;
    bool closed = false;
    void write(const simulation::SimulationEvent& e) { std::lock_guard<std::mutex> g(m); if (!closed) w->Write(e); }
    void close(const simulation::SimulationEvent& e) { std::lock_guard<std::mutex> g(m); if (!closed) w->Write(e); closed = true; }
};
}  // namespace

void SimulationServiceImpl::registerJob(const std::string& token, std::shared_ptr<ActiveJob> job) {
    if (token.empty()) return;
    std::lock_guard<std::mutex> lock(jobMapMutex_);
    activeJobs_[token] = job;
}

void SimulationServiceImpl::unregisterJob(const std::string& token) {
    if (token.empty()) return;
    std::lock_guard<std::mutex> lock(jobMapMutex_);
    activeJobs_.erase(token);
}

std::shared_ptr<SimulationServiceImpl::ActiveJob> SimulationServiceImpl::getJob(const std::string& token) {
    if (token.empty()) return nullptr;
    std::lock_guard<std::mutex> lock(jobMapMutex_);
    if (auto it = activeJobs_.find(token); it != activeJobs_.end()) {
        return it->second;
    }
    return nullptr;
}

grpc::Status SimulationServiceImpl::SubmitJob(
    grpc::ServerContext* context, 
    const simulation::SimulationRequest* request, 
    grpc::ServerWriter<simulation::SimulationEvent>* writer)
{
    auto dto = ProtoMapper::fromProto(*request);

    std::mutex mtx;
    StreamGuard out{writer};  // before the job: its driver (and worker threads) goes first
    std::condition_variable cv;
    bool finished = false;

    auto job = std::make_shared<ActiveJob>();
    job->driver = std::make_unique<LocalWorkerDriver>();

    job->driver->onJobCompleted = [&](JobID, const SimulationResponseDTO& res) {
        simulation::SimulationEvent event;
        event.set_session_token(dto.sessionToken);
        *event.mutable_run_result() = ProtoMapper::toProto(res);
        out.write(event);

        std::lock_guard<std::mutex> lock(mtx);
        finished = true;
        cv.notify_one();
    };

    job->driver->onJobProgress = [&](JobID, float pct, const std::string& msg) {
        simulation::SimulationEvent event;
        event.set_session_token(dto.sessionToken);
        auto* prog = event.mutable_progress();
        prog->set_percent(pct);
        prog->set_message(msg);
        out.write(event);
    };

    job->internalId = job->driver->submitJob(dto);
    registerJob(dto.sessionToken, job);

    std::unique_lock<std::mutex> lock(mtx);
    // Wake up if finished OR client disconnected/cancelled
    cv.wait(lock, [&]{ return finished || context->IsCancelled(); });

    if (context->IsCancelled() && !finished) {
        job->driver->cancelJob(job->internalId);
    }
    
    unregisterJob(dto.sessionToken);

    simulation::SimulationEvent done;
    done.set_session_token(dto.sessionToken);
    done.set_is_finished(true);
    out.close(done);

    return grpc::Status::OK;
}

grpc::Status SimulationServiceImpl::SubmitBatchJob(
    grpc::ServerContext* context, 
    const simulation::SimulationBatchRequest* request, 
    grpc::ServerWriter<simulation::SimulationEvent>* writer)
{
    auto dto = ProtoMapper::fromProto(*request);

    std::mutex mtx;
    StreamGuard out{writer};  // before the job: its driver (and worker threads) goes first
    std::condition_variable cv;
    bool finished = false;

    auto job = std::make_shared<ActiveJob>();
    job->driver = std::make_unique<LocalWorkerDriver>();

    job->driver->onBatchCompleted = [&](JobID, const SimulationBatchResponseDTO& batch) {
        simulation::SimulationEvent event;
        event.set_session_token(dto.sessionToken);
        *event.mutable_batch_summary() = ProtoMapper::toProtoBatchSummary(batch);
        out.write(event);

        std::lock_guard<std::mutex> lock(mtx);
        finished = true;
        cv.notify_one();
    };

    job->driver->onJobCompleted = [&](JobID, const SimulationResponseDTO& res) {
        simulation::SimulationEvent event;
        event.set_session_token(dto.sessionToken);
        *event.mutable_run_result() = ProtoMapper::toProto(res);
        out.write(event);
    };

    job->driver->onBatchProgress = [&](JobID, uint32_t completed, uint32_t total) {
        simulation::SimulationEvent event;
        event.set_session_token(dto.sessionToken);
        auto* bp = event.mutable_batch_progress();
        bp->set_completed(completed);
        bp->set_total(total);
        out.write(event);
    };

    job->internalId = job->driver->submitBatchJob(dto);
    registerJob(dto.sessionToken, job);

    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [&]{ return finished || context->IsCancelled(); });

    if (context->IsCancelled() && !finished) {
        job->driver->cancelJob(job->internalId);
    }

    unregisterJob(dto.sessionToken);

    simulation::SimulationEvent done;
    done.set_session_token(dto.sessionToken);
    done.set_is_finished(true);
    out.close(done);

    return grpc::Status::OK;
}

grpc::Status SimulationServiceImpl::SubmitStatisticalJob(
    grpc::ServerContext* context, 
    const simulation::StatisticalJobRequest* request, 
    grpc::ServerWriter<simulation::SimulationEvent>* writer)
{
    auto baseReq = ProtoMapper::fromProto(request->base_request());
    auto config = ProtoMapper::fromProto(request->mc_config());

    std::mutex mtx;
    StreamGuard out{writer};  // before the job: its driver (and worker threads) goes first
    std::condition_variable cv;
    bool finished = false;

    auto job = std::make_shared<ActiveJob>();
    job->driver = std::make_unique<LocalWorkerDriver>();

    job->driver->onBatchCompleted = [&](JobID, const SimulationBatchResponseDTO& batch) {
        simulation::SimulationEvent event;
        event.set_session_token(baseReq.sessionToken);
        *event.mutable_batch_summary() = ProtoMapper::toProtoBatchSummary(batch);
        out.write(event);

        std::lock_guard<std::mutex> lock(mtx);
        finished = true;
        cv.notify_one();
    };

    job->driver->onJobCompleted = [&](JobID, const SimulationResponseDTO& res) {
        simulation::SimulationEvent event;
        event.set_session_token(baseReq.sessionToken);
        *event.mutable_run_result() = ProtoMapper::toProto(res);
        out.write(event);
    };

    job->driver->onBatchProgress = [&](JobID, uint32_t completed, uint32_t total) {
        simulation::SimulationEvent event;
        event.set_session_token(baseReq.sessionToken);
        auto* bp = event.mutable_batch_progress();
        bp->set_completed(completed);
        bp->set_total(total);
        out.write(event);
    };

    job->internalId = job->driver->submitStatisticalJob(baseReq, config);
    registerJob(baseReq.sessionToken, job);

    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [&]{ return finished || context->IsCancelled(); });

    if (context->IsCancelled() && !finished) {
        job->driver->cancelJob(job->internalId);
    }

    unregisterJob(baseReq.sessionToken);

    simulation::SimulationEvent done;
    done.set_session_token(baseReq.sessionToken);
    done.set_is_finished(true);
    out.close(done);

    return grpc::Status::OK;
}

grpc::Status SimulationServiceImpl::CancelJob(
    grpc::ServerContext* context, 
    const simulation::CancelRequest* request, 
    simulation::CancelAck* response)
{
    (void)context;
    response->set_session_token(request->session_token());
    
    if (auto job = getJob(request->session_token())) {
        job->driver->cancelJob(job->internalId);
        response->set_accepted(true);
    } else {
        response->set_accepted(false);
    }
    
    return grpc::Status::OK;
}

grpc::Status SimulationServiceImpl::GetCapabilities(
    grpc::ServerContext* context, 
    const simulation::CapabilitiesRequest* request, 
    simulation::CapabilitiesResponse* response)
{
    (void)context;
    (void)request;

    response->set_server_version("1.0.0");
    response->add_supported_analyses("DC");
    response->add_supported_analyses("TRANSIENT");
    response->add_supported_analyses("AC");
    response->add_supported_analyses("MONTE_CARLO");
    response->set_supports_batch(true);
    response->set_supports_monte_carlo(true);
    response->set_supports_concurrent_jobs(true);
    response->set_max_concurrent_jobs(32);
    
    auto corners = CornerParametersDTO::standard5();
    for (const auto& c : corners) {
        *response->add_available_corners() = ProtoMapper::toProto(c);
    }
    
    response->add_solver_backends("cpu");
    response->add_solver_backends("cpu_simd");
    
    return grpc::Status::OK;
}

} // namespace acutesim::compute::orchestration
