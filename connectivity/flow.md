# Connectivity Layer Architecture

The `connectivity` directory serves as the **Platform Kernel** — the bridge between the high-level GUI/AI surfaces and the low-level Engine API.

## Architectural Goal: Engine-Pure Orchestration
The connectivity layer must be "Engine-Pure", meaning it can only depend on STL, `engine_api/`, and DTOs. It explicitly forbids dependencies on Qt or engine internals (like `compute/` or `physics/`).

## Local Logic: The Adapter Pattern
Connectivity components translate platform-specific requests (e.g., from the terminal or AI) into canonical `engine_api` calls. This ensures that the engine doesn't need to know who is calling it.

## Data Flow: Command Execution
1.  **Ingress**: A command arrives (e.g., via `terminal_parser`).
2.  **Adaptation**: The connectivity component wraps the command into a `SimulationRequestDTO`.
3.  **Handoff**: The request is submitted to `SimulationJobManager`.
4.  **Observation**: The connectivity layer monitors the job and updates the `ResultsDB`.

## 🤖 SME Validation Checklist
- [ ] **Engine-Pure**: Does this component include any Qt header? (Forbidden: Must be STL-only).
- [ ] **No Internals**: Does this component include `compute/` or `physics/` headers? (Forbidden: Must only use `engine_api/`).
- [ ] **Pathless**: Are you using hardcoded filesystem paths? (Forbidden: All paths must be DTO-provided).
- [ ] **Isolation**: Does `tools/check_connectivity_isolation.py` pass?
