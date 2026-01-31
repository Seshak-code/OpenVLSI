#include <iostream>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <cassert>
#include "compute/orchestration/grpc_worker_client.h"
#include "compute/orchestration/server/subprocess.h" // Reuse subprocess to spawn worker

using namespace acutesim::worker;
using namespace acutesim::orchestration;

// Simple test to verify gRPC transport
int main() {
    std::cout << "[TEST] Starting acutesim-worker..." << std::endl;
    
    // 1. Spawn the worker server in the background
    // We assume the worker executable is in the same directory or PATH
    // Actually, for CTest/cmake, it's in the build directory. We'll try relative path.
    // NOTE: This assumes we are running from the build root or valid location.
    // In a real test harness, we'd pass the path to the worker executable.
    
    // We'll use popen or system for simplicity in this standalone test or use the Subprocess class if linked.
    // Since Subprocess is part of the server lib, let's just use std::system with & for now 
    // or better, rely on manual startup for this quick verification or use fork/exec.
    
    // However, since we linked subprocess.cpp to the worker, we can't easily link it here without duplicating.
    // Let's use a simple background launch command.
    std::string workerCmd = "./build_worker/acutesim-worker > worker.log 2>&1 & echo $! > worker.pid";
    int ret = std::system(workerCmd.c_str());
    if (ret != 0) {
        std::cerr << "[FAIL] Failed to launch worker." << std::endl;
        return 1;
    }

    // Give it a moment to start
    std::this_thread::sleep_for(std::chrono::seconds(2));

    try {
        std::cout << "[TEST] Connecting to localhost:50051..." << std::endl;
        WorkerClient client("localhost:50051");

        // 2. Test Connection
        if (!client.connect()) {
            std::cerr << "[FAIL] Failed to connect to worker." << std::endl;
            std::system("kill $(cat worker.pid) && rm worker.pid");
            return 1;
        }
        std::cout << "[PASS] Connected!" << std::endl;

        // 3. Test GetStatus
        WorkerStatus status = client.getStatus();
        std::cout << "[INFO] Worker ID: " << status.workerId << std::endl;
        std::cout << "[INFO] Version: " << status.version << std::endl;
        std::cout << "[INFO] CPU Usage: " << status.cpuUsage << std::endl;
        
        if (!status.ready) {
             std::cerr << "[FAIL] Worker reports not ready." << std::endl;
             std::system("kill $(cat worker.pid) && rm worker.pid");
             return 1;
        }

        std::cout << "[PASS] GetStatus successful." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "[FAIL] Exception: " << e.what() << std::endl;
        std::system("kill $(cat worker.pid) && rm worker.pid");
        return 1;
    }

    // Cleanup
    std::cout << "[TEST] Cleaning up..." << std::endl;
    std::system("kill $(cat worker.pid) && rm worker.pid");
    
    return 0;
}
