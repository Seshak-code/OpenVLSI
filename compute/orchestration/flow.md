# Orchestration Flow

The `compute/orchestration/` directory is the **Control Plane** for simulation execution and external compute resources.
It implements both a **Headless Simulation Driver** (DTO-based, GUI-agnostic) and a **Hybrid Cloud/Local** execution model for external EDA tools.

It serves as the **Middleware** layer connecting three specialized sub-systems:
1.  `dto/`: Pure data contract structs (`SimulationRequestDTO`, `SimulationBatchRequestDTO`, `StatisticalSummaryDTO`). **No Qt, No Eigen, No Physics types.**
2.  `driver/`: Simulation execution runtimes (`ISimulationDriver`, `LocalWorkerDriver`, `BatchExecutionEngine`, `RemoteGrpcDriver`). **The ONLY place `CircuitSim` is instantiated (inside `LocalWorkerDriver`).**
3.  `drivers/`: External EDA tool payload generators (Yosys, OpenROAD, KLayout).
4.  `proto/`: gRPC wire format definitions — `worker_service.proto` (EDA tools) and `simulation_service.proto` (simulation jobs).

> [!CAUTION] Architectural Invariant
> **The GUI (`gui/`) must NEVER link to or include headers from `physics/` or `compute/solvers/`.** All simulation interaction must go through the `ISimulationDriver` interface using DTOs, whether it's a single run or a Monte Carlo statistical batch. This is enforced via CMake linkage rules.

## Architecture Stack

### 1. The Semantic Layer (`drivers/`)
(See [drivers/flow.md](drivers/flow.md))
*   **Role**: **Payload Generation**.
*   **Input**: High-level intent (e.g., `SynthesisGoal::SPEED`).
*   **Output**: Executable strings (TCL/Shell scripts).
*   **Philosophy**: "Configuration-to-Script". It isolates AcuteSim from the syntax needed to drive tools like Yosys.

### 2. The Transport Layer (`proto/`)
(See [proto/flow.md](proto/flow.md))
*   **Role**: **Contracts**.
*   **Artifacts**:
    *   `worker_service.proto` — EDA tool execution (Yosys/OpenROAD/KLayout). Streams log lines + file artifacts.
    *   `simulation_service.proto` — Structured simulation execution (DC/TRAN/AC/MC). Streams typed `SimulationEvent` messages mapping 1-to-1 to `simulation_dto.h` types.
*   **Behavior**: All long-running operations use server-streaming RPCs for real-time feedback.

### 3. The Client Layer (`./`)
*   **Key File**: `grpc_worker_client.h` / `.cpp`.
*   **Role**: **Dispatcher**.
*   **Behavior**: Dispatches tasks to the `WorkerService` and streams logs back to the GUI.

### 4. The Worker Agent (`server/`)
*   **Key File**: `worker_server_main.cpp`, `worker_service_impl.h`.
*   **Role**: **Executor**.
*   **Behavior**: A headless C++ executable (`acutesim-worker`) that listens for gRPC commands, manages sandboxed subprocesses, and streams execution telemetry.

### 5. The Parity Harness (`./`)
*   **Key File**: `parity_harness.py`.
*   **Role**: **Verifier**.
*   **Behavior**: Orchestrates cross-platform numerical validation, asserting 0-bit deviation between the local engine and the remote worker.

## RTL-to-GDS Stage DAG (Formal, Phase 3.5)

```
Yosys (synthesis: RTL → gate-level netlist)
  └→ OpenROAD (floorplan → global placement → detailed placement → CTS → routing → STA)
       ├→ KLayout DRC          (design rule check vs. foundry rules)
       ├→ KLayout + netgen LVS (layout vs. schematic comparison)       ← Phase 3.5
       ├→ OpenROAD RCX (PEX)   (parasitic extraction → SPEF)           ← Phase 3.5
       └→ KLayout GDSII Export (final .gds[.gz] tapeout artifact)      ← Phase 3.5
```

**Worker RPCs (worker_service.proto)**:
| Stage | RPC | Request |
|-------|-----|---------|
| Synthesis | `RunSynthesis` | `SynthesisRequest` |
| P&R | `RunPlaceAndRoute` | `PnRRequest` |
| Sign-Off (generic) | `RunSignOff` | `SignOffRequest` (check_type="DRC"\|"LVS"\|"PEX") |
| LVS (explicit) | `RunLVS` | `LVSRequest` |
| PEX (explicit) | `RunPEX` | `PEXRequest` (engine="openroad_rcx"\|"klayout_pex") |
| GDSII Export | `RunGDSExport` | `GDSExportRequest` |

## The Execution Pipeline

1.  **User Action**: User clicks "Synthesize" or "Verify Parity".
2.  **Driver Step**: `YosysTask::generateTcl` or `RegressionRequest` payload is prepared.
3.  **Client Step**: `WorkerClient::runSynthesis` or `RunRegression` bundles the request.
4.  **Transport**: Request travels to an `acutesim-worker` instance.
5.  **Execution**: Worker runs the tool via the `Subprocess` utility.
6.  **Streaming**: Logs and pass/fail verdicts flow back in real-time.

## Core Logic & Snippets

### The Physics/GUI Decoupling Bridge
```cpp
// 1. GUI creates pure data request
SimulationBatchRequestDTO batchReq;
batchReq.baseNetlistJson = schematicEditor->toNetlistJson();
batchReq.analysis = AnalysisTypeDTO::TRANSIENT;

// 2. Transmits over pure interface (Execution stays opaque)
driver->submitBatchJob(batchReq);

// 3. Driver leverages internal BatchExecutionEngine for execution
// Engine uses threaded RunSolver instances to stay hermetic.
```

### The Remote EDA Bridge: Driver -> Proto -> Client

```cpp
// 1. Generate Payload (Driver)
QString tclScript = YosysTask::generateTcl(config);

// 2. Wrap in Proto (Client)
SynthesisRequest req;
req.set_top_module("cpu_core");
req.add_source_files()->set_content(tclScript.toStdString()); // Send script as file

// 3. Dispatch (gRPC)
std::unique_ptr<ClientReader<SynthesisLog>> reader(
    stub_->RunSynthesis(&context, req)
);

// 4. Stream Logs (Orchestration)
while (reader->Read(&log)) {
    ui->appendConsole(log.line());
}
```

## Development Rules

1.  **Separation of Concerns**: `WorkerClient` should never know *syntactic* details of Yosys (that belongs in `drivers/`). It just knows how to shovel bytes.
2.  **PIMPL Isolation**: The `WorkerClient` uses PIMPL to hide `grpc` headers from the rest of AcuteSim.
3.  **Conditional Building**: Entire orchestration layer is guarded by `#ifdef ACUTESIM_ENABLE_ORCHESTRATION` to allow WASM builds (which lack gRPC) to compile dummy clients.

## The Remote Simulation Bridge: RemoteGrpcDriver

When a `RemoteGrpcDriver` is used instead of `LocalWorkerDriver`, the execution
pipeline extends out-of-process:

```
GUI → SimulationWorkspace
    → SimulationDriverAdapter (Qt signals → DTO callbacks)
    → RemoteGrpcDriver::submitJob(SimulationRequestDTO)
        → ProtoMapper::toProto(dto)  →  simulation_service.proto SimulationRequest
        → gRPC channel → SimulationService.SubmitJob (streaming RPC)
        → background thread drains SimulationEvents from stream
        → ProtoMapper::fromProto(event) → SimulationResponseDTO
        → fires onJobCompleted / onBatchCompleted callbacks
```

**Key invariant**: The GUI sees identical callbacks regardless of whether
`LocalWorkerDriver` or `RemoteGrpcDriver` is active. The driver is a compile-time
or runtime configuration choice transparent to `SimulationWorkspace`.

**Deterministic MC over gRPC**: The `seed` field in `MonteCarloConfig` proto
maps to `MonteCarloConfigDTO::seed`. The server uses the same `BatchExecutionEngine`
and `std::mt19937(seed)`. Seed 42 on any machine produces bit-identical results.

**Cancellation flow**:
```
GUI calls driver.cancelJob(jobId)
  → RemoteGrpcDriver::Impl looks up session_token from jobId
  → sends SimulationService.CancelJob(session_token)  [unary, non-blocking]
  → server sets atomic cancelRequested_ flag in BatchExecutionEngine
  → stream drains remaining events (partial batch results preserved)
  → stream closes → onBatchCompleted fires with hadFailures=true
```

## 🤖 SME Validation Checklist
*(Consult this list before modifying `compute/orchestration/`)*

- [ ] **WASM Safety**: Is new gRPC code guarded by `#ifdef ACUTESIM_ENABLE_ORCHESTRATION`?
- [ ] **Logic Separation**: Does `WorkerClient` contain tool-specific syntax (TCL/Verilog)? (Forbidden: Move to `drivers/`).
- [ ] **Header Hygiene**: Are Protobuf/gRPC headers leaking into public `.h` files? (Forbidden: Use PIMPL).
- [ ] **Async UI**: Does the status callback guarantee thread safety (invoking on GUI thread)?
- [ ] **Proto Backward Compat**: No field IDs renumbered in `simulation_service.proto`?
- [ ] **DTO Parity**: New DTO fields have a corresponding proto field (same default value semantics)?
- [ ] **Seed Determinism**: MC seed flows through `MonteCarloConfig.seed` → `BatchExecutionEngine` unchanged?
