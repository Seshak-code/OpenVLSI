#pragma once

#include "worker_service.grpc.pb.h"
#include "subprocess.h"
#include <grpcpp/grpcpp.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>

namespace acutesim::orchestration {

using grpc::ServerContext;
using grpc::Status;
using grpc::ServerWriter;

using namespace acutesim::worker;

namespace fs = std::filesystem;

class WorkerServiceImpl final : public WorkerService::Service {
public:
    WorkerServiceImpl() {
        // Initialize capabilities on startup
        detectTools();
    }

    Status GetStatus(ServerContext* context, const StatusRequest* request, StatusResponse* response) override {
        response->set_worker_id("local-worker-01");
        response->set_version("0.1.0-alpha");
        response->set_ready(true);
        response->set_cpu_usage(0.1); // Placeholder: Accessing OS stats is platform-specific
        response->set_memory_usage(128.0); 

        // Populate tool map
        auto* toolsMap = response->mutable_tools();
        for (const auto& [name, ver] : tools_) {
            (*toolsMap)[name] = ver;
        }

        return Status::OK;
    }

    Status RunSynthesis(ServerContext* context, const SynthesisRequest* request, ServerWriter<SynthesisLog>* writer) override {
        std::string job_id = request->job_id();
        if (job_id.empty()) {
            // Generate UUID if missing (simplified)
            job_id = "job-" + std::to_string(std::rand()); 
        }

        // 1. Setup Sandbox
        fs::path sandboxDir = fs::current_path() / "sandbox" / job_id;
        try {
            fs::create_directories(sandboxDir);
        } catch (const fs::filesystem_error& e) {
            return Status(grpc::INTERNAL, std::string("Failed to create sandbox: ") + e.what());
        }

        // 2. Write Source Files
        for (const auto& file : request->source_files()) {
            fs::path filePath = sandboxDir / file.filename();
            std::ofstream ofs(filePath, std::ios::binary);
            ofs.write(file.content().data(), file.content().size());
        }

        // 3. Prepare Command
        // We assume the input provided is a script file or a source + top module
        // For Phase 0, we assume source_files[0] is the TCL script if extension is .tcl
        if (request->source_files().empty()) {
             return Status(grpc::INVALID_ARGUMENT, "No source files provided");
        }
        
        std::string scriptName = request->source_files(0).filename();
        
        // 4. Run Yosys
        Subprocess proc("yosys", {"-s", scriptName});
        proc.setWorkingDirectory(sandboxDir.string());
        proc.setTimeout(std::chrono::minutes(10)); // Watchdog

        SubprocessResult res = proc.runWithStreaming(
            [&](const std::string& line) {
                SynthesisLog log;
                log.set_job_id(job_id);
                log.set_log_line(line);
                writer->Write(log);
            },
            [&](const std::string& line) {
                SynthesisLog log;
                log.set_job_id(job_id);
                log.set_log_line("ERR: " + line);
                log.set_is_error(true);
                writer->Write(log);
            }
        );

        // 5. Finalize
        SynthesisLog endLog;
        endLog.set_job_id(job_id);
        endLog.set_is_finished(true);
        
        if (res.timed_out) {
            endLog.set_is_error(true);
            endLog.set_log_line("Job Timed Out (Watchdog Violation)");
            writer->Write(endLog);
            return Status(grpc::DEADLINE_EXCEEDED, "Job timed out");
        } else if (res.exit_code != 0) {
            endLog.set_is_error(true);
            endLog.set_log_line("Process exited with code " + std::to_string(res.exit_code));
            writer->Write(endLog);
            return Status::OK; // We return OK so client receives the log, but log indicates error
        }

        writer->Write(endLog);
        return Status::OK;
    }

    Status RunRegression(ServerContext* context, const RegressionRequest* request, ServerWriter<RegressionLog>* writer) override {
        std::string job_id = request->job_id();
        std::string suite = request->suite_name();
        std::string mode = request->solver_mode();
        std::string prec = request->precision();
        
        // 1. Determine executable and arguments
        // Check standard locations. Prefer unified 'build/' directory.
        std::vector<std::string> searchPaths = {"./build/", "./"};
        std::string baseName = "test_regression";
        
        if (suite == "fpu_determinism") {
             baseName = "test_fpu_determinism";
        } else if (suite == "determinism_replay") {
             baseName = "test_determinism";
        } else if (suite == "all") {
             baseName = "test_worker_connectivity"; // Fallback/Placeholder
        }

        std::string exePath;
        bool found = false;
        for (const auto& prefix : searchPaths) {
            std::string p = prefix + baseName;
            if (fs::exists(p)) {
                exePath = p;
                found = true;
                break;
            }
        }
        
        if (!found) {
            // Last ditch fallback
             exePath = "./build/test_worker_connectivity"; 
        }

        // Pass metadata as CLI flags if the executable supports them
        // Note: For now, we assume the test executables (except legacy ones) might ignore unknown flags or we need to update them.
        // For 'test_determinism', it takes no args yet, but 'parity_harness' expects logic.
        // We will just log them for now in the worker log to confirm receipt.
        
        std::string logMsg = "Starting suite: " + suite + " mode: " + (mode.empty() ? "default" : mode) + " precision: " + (prec.empty() ? "default" : prec);
        RegressionLog startLog;
        startLog.set_job_id(job_id);
        startLog.set_log_line(logMsg);
        writer->Write(startLog);
        
        // 2. Spawn Process
        std::vector<std::string> args;
        Subprocess proc(exePath, args);
        // proc.setWorkingDirectory(...) // Optional
        proc.setTimeout(std::chrono::minutes(5)); 

        SubprocessResult res = proc.runWithStreaming(
            [&](const std::string& line) {
                RegressionLog log;
                log.set_job_id(job_id);
                log.set_log_line(line);
                writer->Write(log);
            },
            [&](const std::string& line) {
                RegressionLog log;
                log.set_job_id(job_id);
                log.set_log_line("ERR: " + line);
                log.set_is_error(true);
                writer->Write(log);
            }
        );

        // 3. Finalize
        RegressionLog endLog;
        endLog.set_job_id(job_id);
        endLog.set_is_finished(true);
        
        if (res.timed_out) {
            endLog.set_is_error(true);
            endLog.set_log_line("Regression Timed Out");
            endLog.set_passed(false);
        } else {
            endLog.set_passed(res.exit_code == 0);
        }

        writer->Write(endLog);
        return Status::OK;
    }

private:
    std::map<std::string, std::string> tools_;

    void detectTools() {
        // Detect Yosys
        Subprocess yosys("yosys", {"-V"});
        auto res = yosys.run();
        if (res.exit_code == 0 && !res.stdout_output.empty()) {
            // Output example: "Yosys 0.9 (git sha1...)"
            tools_["yosys"] = res.stdout_output.substr(0, 20) + "..."; // Truncate
        }
    }
};

} // namespace acutesim::orchestration
