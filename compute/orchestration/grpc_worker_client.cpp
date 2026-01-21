#include "grpc_worker_client.h"

// Check if orchestration is enabled in CMake
#ifdef ACUTESIM_ENABLE_ORCHESTRATION

#include "worker_service.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <iostream>

namespace acutesim::worker {

class WorkerClient::Impl {
public:
    Impl(std::shared_ptr<grpc::Channel> channel)
        : stub_(WorkerService::NewStub(channel)) {}

    std::unique_ptr<WorkerService::Stub> stub_;
};

WorkerClient::WorkerClient(std::shared_ptr<void> channel_ptr) {
    // Cast void* back to grpc::Channel
    auto channel = std::static_pointer_cast<grpc::Channel>(channel_ptr);
    m_impl = std::make_unique<Impl>(channel);
}

WorkerClient::WorkerClient(const std::string& target) {
    auto channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
    m_impl = std::make_unique<Impl>(channel);
}

WorkerClient::~WorkerClient() = default;

bool WorkerClient::connect() {
    // Simple ping mechanism using GetStatus
    // In gRPC, connection is often lazy, so we try a call.
    try {
        getStatus();
        return true;
    } catch (...) {
        return false;
    }
}

WorkerStatus WorkerClient::getStatus() {
    StatusRequest request;
    StatusResponse response;
    grpc::ClientContext context;

    grpc::Status status = m_impl->stub_->GetStatus(&context, request, &response);

    if (status.ok()) {
        std::vector<std::string> tools;
        for (const auto& tool : response.supported_tools()) {
            tools.push_back(tool);
        }
        return {
            response.worker_id(),
            response.version(),
            response.ready(),
            response.cpu_usage(),
            tools
        };
    } else {
        throw std::runtime_error("RPC failed: " + status.error_message());
    }
}

void WorkerClient::runSynthesis(const JobConfig& config, LogCallback onLog, CompletionCallback onComplete) {
    // This should ideally be async or run in a separate thread to not block UI.
    // For MVP, we'll demonstrate the synchronous stream reading.
    
    grpc::ClientContext context;
    SynthesisRequest request;
    request.set_job_id(config.jobId);
    request.set_top_module(config.topModule);
    
    // Load files (simplified for example)
    for (const auto& path : config.sourceFiles) {
        auto* f = request.add_source_files();
        f->set_filename(path);
        // In real app, read file content here
    }

    std::unique_ptr<grpc::ClientReader<SynthesisLog>> reader(
        m_impl->stub_->RunSynthesis(&context, request));

    SynthesisLog logMsg;
    bool success = false;
    std::string artifactPath; // Placeholder for netlist

    while (reader->Read(&logMsg)) {
        if (onLog) {
            onLog(logMsg.log_line(), logMsg.is_error());
        }
        
        if (logMsg.is_finished()) {
            // Check for artifacts
            if (logMsg.has_netlist()) {
                artifactPath = "RAM: " + logMsg.netlist().filename(); 
            }
        }
    }

    grpc::Status status = reader->Finish();
    if (status.ok()) {
        if (onComplete) onComplete(true, artifactPath);
    } else {
        if (onLog) onLog("RPC Error: " + status.error_message(), true);
        if (onComplete) onComplete(false, "");
    }
}

void WorkerClient::runPlaceAndRoute(const JobConfig& config, LogCallback onLog, CompletionCallback onComplete) {
    // Similar implementation for PnR
}

} // namespace acutesim::worker

#else

// Dummy implementation for builds without orchestration (e.g. WASM)
namespace acutesim::worker {
    class WorkerClient::Impl {};
    WorkerClient::WorkerClient(std::shared_ptr<void>) { m_impl = std::make_unique<Impl>(); }
    WorkerClient::WorkerClient(const std::string&) { m_impl = std::make_unique<Impl>(); }
    WorkerClient::~WorkerClient() = default;
    bool WorkerClient::connect() { return false; }
    WorkerStatus WorkerClient::getStatus() { return {}; }
    void WorkerClient::runSynthesis(const JobConfig&, LogCallback, CompletionCallback) {}
    void WorkerClient::runPlaceAndRoute(const JobConfig&, LogCallback, CompletionCallback) {}
}

#endif
