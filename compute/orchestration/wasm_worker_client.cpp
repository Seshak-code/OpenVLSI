#include "grpc_worker_client.h"
#include <iostream>

namespace acutesim::worker {

class WorkerClient::Impl {
public:
    // Empty stub
};

WorkerClient::WorkerClient(std::shared_ptr<void> channel) : m_impl(std::make_unique<Impl>()) {}
WorkerClient::WorkerClient(const std::string& target) : m_impl(std::make_unique<Impl>()) {}
WorkerClient::~WorkerClient() = default;

bool WorkerClient::connect() { return false; }

WorkerStatus WorkerClient::getStatus() {
    WorkerStatus status;
    status.workerId = "WASM-Stub";
    status.version = "0.0.0";
    status.ready = false;
    status.cpuUsage = 0.0;
    return status;
}

void WorkerClient::runSynthesis(const JobConfig& config, LogCallback onLog, CompletionCallback onComplete) {
    if (onLog) onLog("Synthesis not supported in WASM", true);
    if (onComplete) onComplete(false, "");
}

void WorkerClient::runPlaceAndRoute(const JobConfig& config, LogCallback onLog, CompletionCallback onComplete) {
    if (onLog) onLog("PnR not supported in WASM", true);
    if (onComplete) onComplete(false, "");
}

void WorkerClient::runPlaceAndRoute(const PnRJobConfig& config, LogCallback onLog, ProgressCallback onProgress, CompletionCallback onComplete) {
    if (onLog) onLog("PnR not supported in WASM", true);
    if (onComplete) onComplete(false, "");
}

void WorkerClient::runHDLSimulation(const JobConfig& config, LogCallback onLog, CompletionCallback onComplete) {
    if (onLog) onLog("HDL Sim not supported in WASM", true);
    if (onComplete) onComplete(false, "");
}

void WorkerClient::runSignOff(const SignOffJobConfig& config, LogCallback onLog, CompletionCallback onComplete) {
    if (onLog) onLog("SignOff not supported in WASM", true);
    if (onComplete) onComplete(false, "");
}

} // namespace acutesim::worker
