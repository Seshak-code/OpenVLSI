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

struct SynthesisJobConfig : JobConfig {
    std::string targetLibrary;    // e.g. "sky130_fd_sc_hd"
    std::string optimizationGoal; // "AREA", "SPEED", "POWER", "BALANCED"
    std::string tclScript;        // Pre-generated TCL script content
};

struct PnRJobConfig {
    std::string jobId;
    std::string netlistContent;   // Synthesized Verilog netlist
    std::string constraintFile;   // SDC content
    double dieWidth = 0.0;
    double dieHeight = 0.0;
    double coreWidth = 0.0;
    double coreHeight = 0.0;
    std::string tclScript;        // Pre-generated TCL script content
};

struct SignOffJobConfig {
    std::string jobId;
    std::string layoutFile;       // GDS/OASIS file path or content
    std::string scriptContent;    // Ruby/DRC script content
    std::string checkType;        // "DRC", "LVS", "PEX"
};

using ProgressCallback = std::function<void(float progress)>;

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

    // Extended RPCs with richer configs
    void runPlaceAndRoute(const PnRJobConfig& config, LogCallback onLog, ProgressCallback onProgress, CompletionCallback onComplete);
    void runHDLSimulation(const JobConfig& config, LogCallback onLog, CompletionCallback onComplete);
    void runSignOff(const SignOffJobConfig& config, LogCallback onLog, CompletionCallback onComplete);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl; // PIMPL idiom to hide gRPC details
};

} // namespace acutesim::worker
