# Orchestration Driver Architecture

The `compute/orchestration/driver` directory contains the **System Actuators** — the concrete implementations of the `ISimulationDriver` interface.

## Logic: Backend Implementation
This is where the platform communicates with specific solvers or worker loops.
-   [**local_worker_driver**]: Orchestrates solvers within the same memory space.
-   [**remote_grpc_driver**]: Proxies requests to an external worker service.

## 🤖 SME Validation Checklist
- [ ] **Interface Strictness**: Does the driver implement all methods of `ISimulationDriver`?
- [ ] **Error Propagation**: Are solver errors correctly caught and mapped to `SimulationResponseDTO` failure states?
