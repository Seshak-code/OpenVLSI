#pragma once

#include <string>
#include <memory>
#include <vector>
#include <functional>

// Forward declarations for gRPC generated classes (to avoid including grpc headers here directly if possible, 
// though typically we need some for the channel).
// For now, we'll use a PIMPL or abstract interface to keep build times fast.

namespace acutesim::worker {

struct WorkerStatus {
    std::string workerId;
    std::string version;
    bool ready;
    double cpuUsage;
    std::vector<std::string> supportedTools;
};

struct JobConfig {
    std::string jobId;
    std::vector<std::string> sourceFiles; // Paths to source files
    std::string topModule;
    // ... other generic config
};

class WorkerClient {
public:
    WorkerClient(std::shared_ptr<void> channel); // generic void* to avoid grpc dependency in header? Or just use std::string target
    WorkerClient(const std::string& target); // e.g. "localhost:50051"
    ~WorkerClient();

    // Connection Check
    bool connect();
    WorkerStatus getStatus();

    // Async Job Submission
    // Callback is called for every log line received from the stream
    using LogCallback = std::function<void(const std::string& line, bool isError)>;
    using CompletionCallback = std::function<void(bool success, const std::string& artifactPath)>;

    void runSynthesis(const JobConfig& config, LogCallback onLog, CompletionCallback onComplete);
    void runPlaceAndRoute(const JobConfig& config, LogCallback onLog, CompletionCallback onComplete);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl; // PIMPL idiom to hide gRPC details
};

} // namespace acutesim::worker
