#pragma once

#include "isimulation_driver.h"
#include <memory>
#include <string>

namespace acutesim::compute::orchestration {

struct RemoteGrpcDriverOpts {
    std::string target = "localhost:50051";
    int deadline_ms = 0; // 0 = no deadline
};

class RemoteGrpcDriver : public ISimulationDriver {
public:
    explicit RemoteGrpcDriver(const RemoteGrpcDriverOpts& opts = {});
    ~RemoteGrpcDriver() override;

    JobID submitJob(const SimulationRequestDTO& request) override;
    void cancelJob(JobID id) override;

    JobID submitBatchJob(const SimulationBatchRequestDTO& req) override;
    JobID submitStatisticalJob(const SimulationRequestDTO& baseReq, const MonteCarloConfigDTO& config) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace acutesim::compute::orchestration
