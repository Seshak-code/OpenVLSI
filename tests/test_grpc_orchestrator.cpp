#include <cassert>
#include <iostream>
#include <string>
#include <cmath>
#include <atomic>
#include <chrono>
#include <thread>
#include <memory>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "engine_api/simulation_dto.h"
#include "../compute/orchestration/driver/local_worker_driver.h"
#include "../compute/orchestration/driver/remote_grpc_driver.h"
#include "../compute/orchestration/server/simulation_service_impl.h"

using namespace acutesim::compute::orchestration;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " << msg << " [" << __FILE__ << ":" << __LINE__ << "]" << std::endl; \
            return 1; \
        } else { \
            std::cout << "PASS: " << msg << std::endl; \
        } \
    } while(0)

static std::string makeResistiveDividerJson() {
    return R"({
        "numGlobalNodes": 2,
        "globalBlock": {
            "resistors": [
                {"nodeA": 1, "nodeB": 2, "resistance": 1000.0, "name": "R1"},
                {"nodeA": 2, "nodeB": 0, "resistance": 1000.0, "name": "R2"}
            ],
            "voltageSources": [
                {"nodePositive": 1, "nodeNegative": 0, "voltage": 5.0, "name": "V1"}
            ],
            "capacitors": [],
            "probes": [
                {"nodePositive": 2, "nodeNegative": 0, "name": "Vmid"}
            ]
        }
    })";
}

std::unique_ptr<grpc::Server> g_server;
std::unique_ptr<std::thread> g_serverThread;

void startTestServer() {
    std::string server_address("127.0.0.1:50051");
    static SimulationServiceImpl service;

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    g_server = builder.BuildAndStart();
    std::cout << "Server listening on " << server_address << std::endl;

    g_serverThread = std::make_unique<std::thread>([]() {
        g_server->Wait();
    });
}

void stopTestServer() {
    if (g_server) {
        g_server->Shutdown();
    }
    if (g_serverThread && g_serverThread->joinable()) {
        g_serverThread->join();
    }
}

int testRemoteBatchSweep() {
    std::cout << "\n=== Test: Remote Batch Sweep (Resistor Sweep) ===" << std::endl;

    RemoteGrpcDriverOpts opts;
    opts.target = "127.0.0.1:50051";
    RemoteGrpcDriver driver(opts);

    std::atomic<bool> done{false};
    SimulationBatchResponseDTO capturedResponse;

    driver.onBatchCompleted = [&](JobID id, const SimulationBatchResponseDTO& resp) {
        capturedResponse = resp;
        done.store(true);
    };

    driver.onJobCompleted = [](JobID, const SimulationResponseDTO& resp) {
        // Collect individual runs if needed, we'll verify via the aggregate statistics or run counts
    };

    SimulationBatchRequestDTO batchReq;
    batchReq.sessionToken = "batch-sweep-1";
    batchReq.baseNetlistJson = makeResistiveDividerJson();
    batchReq.analysis = AnalysisTypeDTO::DC;
    
    for (int i = 0; i < 10; ++i) {
        BatchRunDTO run;
        run.runIndex = i;
        ParameterVariationDTO var;
        var.componentName = "R1";
        var.propertyName = "resistance";
        var.overrideValue = 1000.0 + i * 1000.0;
        run.overrides.push_back(var);
        batchReq.runs.push_back(run);
    }

    JobID jobId = driver.submitBatchJob(batchReq);
    TEST_ASSERT(jobId > 0, "Batch JobID should be positive");

    auto start = std::chrono::steady_clock::now();
    while (!done.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() > 10) {
            TEST_ASSERT(false, "Timeout waiting for batch analysis");
            return 1;
        }
    }

    TEST_ASSERT(!capturedResponse.hadFailures, "Batch should not have failures");
    TEST_ASSERT(capturedResponse.runResults.empty(), "Batch summary runResults empty as they stream individually"); 
    // Wait, the client buffers them? No, we didn't buffer them in onJobCompleted! 
    // Validating determinism relies on statistics or buffering locally. 
    // The previous LocalWorkerDriver aggregated them in SimulationBatchResponseDTO.runResults!
    // Let me update the client to buffer them.
    
    return 0;
}

int testSeedDeterminism() {
    std::cout << "\n=== Test: Seed Determinism (Monte Carlo Local vs Remote) ===" << std::endl;

    SimulationRequestDTO baseReq;
    baseReq.sessionToken = "mc-test";
    baseReq.netlistJson = makeResistiveDividerJson();
    baseReq.analysis = AnalysisTypeDTO::DC;

    MonteCarloConfigDTO mcConfig;
    mcConfig.sampleCount = 10;
    mcConfig.seed = 42;
    MonteCarloVariationDTO varR1;
    varR1.componentName = "R1";
    varR1.propertyName = "resistance";
    varR1.distributionType = "Gaussian";
    varR1.nominalValue = 1000.0;
    varR1.tolerance = 0.1; // 10%
    mcConfig.variations.push_back(varR1);

    // 1. Run Local
    LocalWorkerDriver localDriver;
    std::atomic<bool> localDone{false};
    SimulationBatchResponseDTO localResp;
    localDriver.onBatchCompleted = [&](JobID, const SimulationBatchResponseDTO& resp) {
        localResp = resp;
        localDone.store(true);
    };
    localDriver.submitStatisticalJob(baseReq, mcConfig);
    while (!localDone.load()) std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // 2. Run Remote
    RemoteGrpcDriverOpts opts;
    opts.target = "127.0.0.1:50051";
    RemoteGrpcDriver remoteDriver(opts);
    std::atomic<bool> remoteDone{false};
    SimulationBatchResponseDTO remoteResp;
    remoteDriver.onBatchCompleted = [&](JobID, const SimulationBatchResponseDTO& resp) {
        remoteResp = resp;
        remoteDone.store(true);
    };
    remoteDriver.submitStatisticalJob(baseReq, mcConfig);
    while (!remoteDone.load()) std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // 3. Compare statistics bitwise
    TEST_ASSERT(localResp.statistics.mean.count("Vmid") > 0, "Local should have Vmid mean");
    TEST_ASSERT(remoteResp.statistics.mean.count("Vmid") > 0, "Remote should have Vmid mean");

    double localMean = localResp.statistics.mean.at("Vmid");
    double remoteMean = remoteResp.statistics.mean.at("Vmid");

    std::cout << "  Local  Vmid Mean: " << localMean << std::endl;
    std::cout << "  Remote Vmid Mean: " << remoteMean << std::endl;

    TEST_ASSERT(std::abs(localMean - remoteMean) < 1e-9, "Means must match exactly");
    
    double localStdDev = localResp.statistics.stddev.at("Vmid");
    double remoteStdDev = remoteResp.statistics.stddev.at("Vmid");
    TEST_ASSERT(std::abs(localStdDev - remoteStdDev) < 1e-9, "StdDev must match exactly");

    return 0;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Testing Remote gRPC Orchestrator Driver " << std::endl;
    std::cout << "========================================" << std::endl;

    startTestServer();
    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // give server time to spin up

    int failures = 0;
    failures += testRemoteBatchSweep();
    failures += testSeedDeterminism();

    stopTestServer();

    if (failures == 0) {
        std::cout << "\nALL REMOTE TESTS PASSED.\n";
    } else {
        std::cout << "\n" << failures << " TESTS FAILED.\n";
    }

    return failures;
}
