# Orchestration Flow

The `compute/orchestration/` directory is the **Control Plane** for external compute resources.
It implements a "Hybrid Cloud/Local" execution model where AcuteSim acts as a head node orchestrating heavy-duty EDA tools (Yosys, OpenROAD, Ngspice).

It serves as the **Middleware** layer connecting two specialized sub-systems:
1.  `drivers/`: Know *What* to run (Semantic Intent -> Shell Script).
2.  `proto/`: Know *How* to transmit it (gRPC Wire Format).

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
*   **Artifact**: `worker_service.proto`.
*   **Behavior**: Defines the `stream` based API for real-time logs and file transfer.

### 3. The Client Layer (`./`)
*   **Key File**: `grpc_worker_client.h` / `.cpp`.
*   **Role**: **Dispatcher**.
*   **Behavior**:
    *   Takes the Script from `drivers/`.
    *   Wraps it in a `SynthesisRequest` protobuf.
    *   Sends it to the `WorkerService` (Local Docker or Remote Cluster).
    *   Streams logs back to the GUI via callbacks.

## The Execution Pipeline

1.  **User Action**: User clicks "Synthesize" in GUI.
2.  **Driver Step**: `YosysTask::generateTcl(config)` creates a 50-line optimization script.
3.  **Client Step**: `WorkerClient::runSynthesis` bundles this script into a gRPC request.
4.  **Transport**: Request travels to a Worker Container (running `acutesim-worker`).
5.  **Execution**: Worker runs `yosys -s script.tcl`.
6.  **Streaming**: Logs ("ABC: optimization...") flow back in real-time to the AcuteSim Console.

## Core Logic & Snippets

### The Bridge: Driver -> Proto -> Client

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

## 🤖 SME Validation Checklist
*(Consult this list before modifying `compute/orchestration/`)*

- [ ] **WASM Safety**: Is new gRPC code guarded by `#ifdef ACUTESIM_ENABLE_ORCHESTRATION`?
- [ ] **Logic Separation**: Does `WorkerClient` contain tool-specific syntax (TCL/Verilog)? (Forbidden: Move to `drivers/`).
- [ ] **Header Hygiene**: Are Protobuf/gRPC headers leaking into public `.h` files? (Forbidden: Use PIMPL).
- [ ] **Async UI**: Does the status callback guarantee thread safety (invoking on GUI thread)?
