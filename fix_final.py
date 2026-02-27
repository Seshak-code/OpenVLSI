with open("compute/orchestration/driver/local_worker_driver.cpp", "r") as f:
    text = f.read()

text = text.replace("LocalWorkerDriver::Impl::LocalWorkerDriver()", "LocalWorkerDriver::LocalWorkerDriver()")
text = text.replace("LocalWorkerDriver::Impl::~LocalWorkerDriver()", "LocalWorkerDriver::~LocalWorkerDriver()")
text = text.replace("JobID LocalWorkerDriver::Impl::submitJob(const SimulationRequestDTO& request) { return impl_->submitJob(request); }", "JobID LocalWorkerDriver::submitJob(const SimulationRequestDTO& request) { return impl_->submitJob(request); }")
text = text.replace("JobID LocalWorkerDriver::Impl::submitBatchJob(const SimulationBatchRequestDTO& req) { return impl_->submitBatchJob(req); }", "JobID LocalWorkerDriver::submitBatchJob(const SimulationBatchRequestDTO& req) { return impl_->submitBatchJob(req); }")
text = text.replace("JobID LocalWorkerDriver::Impl::submitStatisticalJob(const SimulationRequestDTO& baseReq, const MonteCarloConfigDTO& config) { return impl_->submitStatisticalJob(baseReq, config); }", "JobID LocalWorkerDriver::submitStatisticalJob(const SimulationRequestDTO& baseReq, const MonteCarloConfigDTO& config) { return impl_->submitStatisticalJob(baseReq, config); }")
text = text.replace("void LocalWorkerDriver::Impl::cancelJob(JobID id) { impl_->cancelJob(id); }", "void LocalWorkerDriver::cancelJob(JobID id) { impl_->cancelJob(id); }")

text = text.replace("JobID LocalWorkerDriver::Impl::Impl::", "JobID LocalWorkerDriver::Impl::")
text = text.replace("void LocalWorkerDriver::Impl::Impl::", "void LocalWorkerDriver::Impl::")

with open("compute/orchestration/driver/local_worker_driver.cpp", "w") as f:
    f.write(text)
