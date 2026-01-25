# Proto Flow

The `compute/orchestration/proto/` directory contains the **Language-Agnostic Contracts** for the AcuteSim ecosystem.

AcuteSim uses **gRPC (Google Remote Procedure Call)** and **Protocol Buffers (Proto3)** to define how the core C++ engine talks to external compute resources (local Docker containers, remote clusters, or Python-based AI agents).

## Key Files & Responsibilities

### 1. `worker_service.proto`: The EDA Contract
Defines the `WorkerService` API. This is the "Universal Remote" for running EDA tools safely.
*   **Streaming**: All major RPCs (`RunSynthesis`, `RunPlaceAndRoute`, `RunSimulation`) return a `stream` of logs. This allows the GUI to show a real-time terminal output while the heavy tool runs.
*   **Artifacts**: File contents (Netlists, GDSII) are passed as `bytes` in the standard response message.
*   **Universal Tooling**: The message structure is generic enough to support multiple backends (e.g., `RunSynthesis` works for Yosys, ABC, or specialized FPGA tools).

## Core Logic & Snippets

### Service Definition (`worker_service.proto`)
The service defines the capabilities. Note the use of `stream` for long-running tasks.

```protobuf
service WorkerService {
  // Check health and capabilities
  rpc GetStatus (StatusRequest) returns (StatusResponse);

  // Logic Synthesis (RTL -> Gate-Level Netlist)
  // Returns a stream of log lines + final result artifact
  rpc RunSynthesis (SynthesisRequest) returns (stream SynthesisLog);

  // Place & Route (Netlist -> GDSII/DEF)
  rpc RunPlaceAndRoute (PnRRequest) returns (stream PnRLog);
}
```

### Data Transfer (`worker_service.proto`)
Files are encapsulated in a `FileData` message to keep filenames and raw content together.

```protobuf
message FileData {
  string filename = 1;
  bytes content = 2; // Raw binary or text
}

message SynthesisRequest {
  string job_id = 1;
  repeated FileData source_files = 2; // e.g. ["top.v", "alu.v"]
  // ... constraints ...
}
```

## Development Rules

1.  **Backwards Compatibility**: Never renumber fields. If you deprecate a field (e.g., `int32 id = 1;`), mark it `[deprecated=true]` and start a new field at the next available ID.
2.  **Streaming First**: Any task that takes > 100ms should generally return a stream, or at least be designed for async execution.
3.  **Language Agnostic**: Remember this file is used to generate C++, Python, and potentially Go/Rust code. Do not use language-specific features.
4.  **Regeneration**: After modifying `.proto` files, you must run the protobuf compiler (`protoc`) to regenerate the C++ headers in `compute/orchestration/generated/`.
