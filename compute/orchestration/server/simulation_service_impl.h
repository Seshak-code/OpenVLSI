#pragma once

#include "simulation_service.grpc.pb.h"
#include "../driver/local_worker_driver.h"
#include <mutex>
#include <unordered_map>
#include <memory>
#include <string>

namespace acutesim::compute::orchestration {

class SimulationServiceImpl final : public acutesim::simulation::SimulationService::Service {
public:
    SimulationServiceImpl() = default;
    ~SimulationServiceImpl() override = default;

    grpc::Status SubmitJob(
        grpc::ServerContext* context, 
        const simulation::SimulationRequest* request, 
        grpc::ServerWriter<simulation::SimulationEvent>* writer) override;

    grpc::Status SubmitBatchJob(
        grpc::ServerContext* context, 
        const simulation::SimulationBatchRequest* request, 
        grpc::ServerWriter<simulation::SimulationEvent>* writer) override;

    grpc::Status SubmitStatisticalJob(
        grpc::ServerContext* context, 
        const simulation::StatisticalJobRequest* request, 
        grpc::ServerWriter<simulation::SimulationEvent>* writer) override;

    grpc::Status CancelJob(
        grpc::ServerContext* context, 
        const simulation::CancelRequest* request, 
        simulation::CancelAck* response) override;

    grpc::Status GetCapabilities(
        grpc::ServerContext* context, 
        const simulation::CapabilitiesRequest* request, 
        simulation::CapabilitiesResponse* response) override;

private:
    struct ActiveJob {
        std::unique_ptr<LocalWorkerDriver> driver;
        JobID internalId;
    };

    std::mutex jobMapMutex_;
    std::unordered_map<std::string, std::shared_ptr<ActiveJob>> activeJobs_;

    void registerJob(const std::string& token, std::shared_ptr<ActiveJob> job);
    void unregisterJob(const std::string& token);
    std::shared_ptr<ActiveJob> getJob(const std::string& token);
};

} // namespace acutesim::compute::orchestration
