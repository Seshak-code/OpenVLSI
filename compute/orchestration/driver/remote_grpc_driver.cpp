#include <memory>
#include <cstdint>
#include "remote_grpc_driver.h"
#include "../proto_mapper.h"
#include "simulation_service.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>

namespace acutesim::compute::orchestration {

struct RemoteGrpcDriver::Impl {
    RemoteGrpcDriverOpts opts_;
    std::unique_ptr<acutesim::simulation::SimulationService::Stub> stub_;
    std::atomic<uint64_t> nextJobId{1};

    // Keep track of client contexts so we can cancel them
    std::mutex ctxMutex_;
    std::unordered_map<JobID, std::shared_ptr<grpc::ClientContext>> activeContexts_;

    Impl(const RemoteGrpcDriverOpts& opts) : opts_(opts) {
        auto channel = grpc::CreateChannel(opts_.target, grpc::InsecureChannelCredentials());
        stub_ = acutesim::simulation::SimulationService::NewStub(channel);
    }

    std::shared_ptr<grpc::ClientContext> createAndRegisterContext(JobID jobId) {
        auto context = std::make_shared<grpc::ClientContext>();
        if (opts_.deadline_ms > 0) {
            context->set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(opts_.deadline_ms));
        }
        std::lock_guard<std::mutex> lock(ctxMutex_);
        activeContexts_[jobId] = context;
        return context;
    }

    void unregisterContext(JobID jobId) {
        std::lock_guard<std::mutex> lock(ctxMutex_);
        activeContexts_.erase(jobId);
    }

    void cancelJob(JobID jobId) {
        std::lock_guard<std::mutex> lock(ctxMutex_);
        if (auto it = activeContexts_.find(jobId); it != activeContexts_.end()) {
            it->second->TryCancel();
        }
    }
};

RemoteGrpcDriver::RemoteGrpcDriver(const RemoteGrpcDriverOpts& opts)
    : impl_(std::make_unique<Impl>(opts)) {}

RemoteGrpcDriver::~RemoteGrpcDriver() {
    // Note: in a real system we'd cleanly join or detach all threads on shutdown,
    // and TryCancel all active contexts. For brevity, threads are detached.
}

JobID RemoteGrpcDriver::submitJob(const SimulationRequestDTO& request) {
    auto jobId = impl_->nextJobId++;
    
    // Copy callbacks to avoid dangling references if driver dies or changes
    auto cbComplete = onJobCompleted;
    auto cbProgress = onJobProgress;

    std::thread([this, jobId, request, cbComplete, cbProgress]() {
        auto context = impl_->createAndRegisterContext(jobId);
        auto protoReq = ProtoMapper::toProto(request);
        
        auto reader = impl_->stub_->SubmitJob(context.get(), protoReq);
        acutesim::simulation::SimulationEvent event;

        while (reader->Read(&event)) {
            if (event.has_progress() && cbProgress) {
                cbProgress(jobId, event.progress().percent(), event.progress().message());
            }
            if (event.has_run_result() && cbComplete) {
                auto result = ProtoMapper::fromProto(event.run_result());
                cbComplete(jobId, result);
            }
            if (event.has_error() && cbComplete) {
                SimulationResponseDTO errResp;
                errResp.success = false;
                errResp.errorMessage = event.error().code() + ": " + event.error().message();
                cbComplete(jobId, errResp);
            }
            if (event.is_finished()) {
                break;
            }
        }
        
        reader->Finish();
        impl_->unregisterContext(jobId);
    }).detach();

    return jobId;
}

void RemoteGrpcDriver::cancelJob(JobID id) {
    impl_->cancelJob(id);
}

JobID RemoteGrpcDriver::submitBatchJob(const SimulationBatchRequestDTO& req) {
    auto jobId = impl_->nextJobId++;
    
    auto cbBatchComplete = onBatchCompleted;
    auto cbBatchProgress = onBatchProgress;
    auto cbRunComplete = onJobCompleted; // individual runs use this

    std::thread([this, jobId, req, cbBatchComplete, cbBatchProgress, cbRunComplete]() {
        auto context = impl_->createAndRegisterContext(jobId);
        auto protoReq = ProtoMapper::toProto(req);
        
        auto reader = impl_->stub_->SubmitBatchJob(context.get(), protoReq);
        acutesim::simulation::SimulationEvent event;

        while (reader->Read(&event)) {
            if (event.has_batch_progress() && cbBatchProgress) {
                cbBatchProgress(jobId, event.batch_progress().completed(), event.batch_progress().total());
            }
            if (event.has_run_result() && cbRunComplete) {
                auto result = ProtoMapper::fromProto(event.run_result());
                cbRunComplete(jobId, result);
            }
            if (event.has_batch_summary() && cbBatchComplete) {
                auto summary = ProtoMapper::fromProtoBatchSummary(event.batch_summary());
                cbBatchComplete(jobId, summary);
            }
            if (event.has_error() && cbBatchComplete) {
                SimulationBatchResponseDTO errResp;
                errResp.hadFailures = true;
                // DTO doesn't have an errorMessage field on root, so we just set hadFailures
                cbBatchComplete(jobId, errResp);
            }
            if (event.is_finished()) {
                break;
            }
        }
        
        reader->Finish();
        impl_->unregisterContext(jobId);
    }).detach();

    return jobId;
}

JobID RemoteGrpcDriver::submitStatisticalJob(const SimulationRequestDTO& baseReq, const MonteCarloConfigDTO& config) {
    auto jobId = impl_->nextJobId++;
    
    auto cbBatchComplete = onBatchCompleted;
    auto cbBatchProgress = onBatchProgress;
    auto cbRunComplete = onJobCompleted;

    std::thread([this, jobId, baseReq, config, cbBatchComplete, cbBatchProgress, cbRunComplete]() {
        auto context = impl_->createAndRegisterContext(jobId);
        
        acutesim::simulation::StatisticalJobRequest protoReq;
        *protoReq.mutable_base_request() = ProtoMapper::toProto(baseReq);
        *protoReq.mutable_mc_config() = ProtoMapper::toProto(config);
        
        auto reader = impl_->stub_->SubmitStatisticalJob(context.get(), protoReq);
        acutesim::simulation::SimulationEvent event;

        while (reader->Read(&event)) {
            if (event.has_batch_progress() && cbBatchProgress) {
                cbBatchProgress(jobId, event.batch_progress().completed(), event.batch_progress().total());
            }
            if (event.has_run_result() && cbRunComplete) {
                auto result = ProtoMapper::fromProto(event.run_result());
                cbRunComplete(jobId, result);
            }
            if (event.has_batch_summary() && cbBatchComplete) {
                auto summary = ProtoMapper::fromProtoBatchSummary(event.batch_summary());
                cbBatchComplete(jobId, summary);
            }
            if (event.has_error() && cbBatchComplete) {
                SimulationBatchResponseDTO errResp;
                errResp.hadFailures = true;
                cbBatchComplete(jobId, errResp);
            }
            if (event.is_finished()) {
                break;
            }
        }
        
        reader->Finish();
        impl_->unregisterContext(jobId);
    }).detach();

    return jobId;
}

} // namespace acutesim::compute::orchestration
