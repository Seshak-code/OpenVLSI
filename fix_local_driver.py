import re

with open("compute/orchestration/driver/local_worker_driver.cpp", "r") as f:
    text = f.read()

# Replace all LocalWorkerDriver::Impl::Impl:: with LocalWorkerDriver::Impl::
text = text.replace("LocalWorkerDriver::Impl::Impl::", "LocalWorkerDriver::Impl::")

# Replace struct LocalWorkerDriver::Impl::Impl { with struct LocalWorkerDriver::Impl {
text = text.replace("struct LocalWorkerDriver::Impl::Impl {", "struct LocalWorkerDriver::Impl {")

# Remove the SECOND struct LocalWorkerDriver::Impl { ... } entirely
first = text.find("struct LocalWorkerDriver::Impl {")
second = text.find("struct LocalWorkerDriver::Impl {", first + 1)
if second != -1:
    end_second = text.find("};\n", second)
    if end_second != -1:
        text = text[:second] + text[end_second+3:]

# Now, we need to inject the method declarations into the FIRST struct!
pos = text.find("Impl(LocalWorkerDriver* q) : q_ptr(q) {}")
if pos != -1:
    pos_end_struct = text.find("\n};", pos)
    
    methods = """
    JobID submitJob(const SimulationRequestDTO& request);
    JobID submitBatchJob(const SimulationBatchRequestDTO& req);
    JobID submitStatisticalJob(const SimulationRequestDTO& baseReq, const MonteCarloConfigDTO& config);
    void cancelJob(JobID id);
    void workerLoop();
    void executeJob(const InternalJob& job);
    SimulationResponseDTO runDC(const TensorNetlist& netlist, const SimulationRequestDTO& req);
    SimulationResponseDTO runTransient(const TensorNetlist& netlist, const SimulationRequestDTO& req);
    SimulationResponseDTO runAC(const TensorNetlist& netlist, const SimulationRequestDTO& req);
    TensorNetlist deserializeNetlist(const std::string& netlistJson);
"""
    # Only insert if not already there
    if text.find("SimulationResponseDTO runDC") > pos_end_struct or text.find("SimulationResponseDTO runDC") == -1:
        text = text[:pos_end_struct] + methods + text[pos_end_struct:]

# Also fix q_ptr->q_ptr->
text = text.replace("q_ptr->q_ptr->", "q_ptr->")

# We also need to fix executeJob(job) to something else since it's inside Impl?
# Wait, workerLoop is a method of Impl, so executeJob(job) is correct!
# But deserializeNetlist is also a method of Impl, so deserializeNetlist(...) is correct!

# Let's write back
with open("compute/orchestration/driver/local_worker_driver.cpp", "w") as f:
    f.write(text)
