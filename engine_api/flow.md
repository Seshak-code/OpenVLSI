# Engine API Architecture

The `engine_api` directory defines the **Scientific Contract** between the AcuteSim GUI/Client platforms and the Tensor MNA Engine. It serves as the SOLE legal interface for simulation, netlisting, and HDL interactions.

## Architectural Goal: The Isolation Firewall
This layer ensures that the engine can be extracted into a standalone process or repository with zero friction. It prevents "Qt-leakage" into the engine and "Physics-leakage" into the GUI.

## Data Flow: Simulation Life Cycle
1.  **Submission**: Client calls `SimulationJobManager::submit(ClientID, SimulationRequestDTO)`.
2.  **Validation**: `engine_api` ensures the DTO is topologically sound.
3.  **Observation**: `IJobObserver` handles status updates (Queued -> Running -> Completed).
4.  **Retrieval**: `ResultsDB` provides immutable `ResultHandle` (shared_ptr<const>) for plotting.

## Local Logic: The Hardenings
-   **Job Ownership**: Every job MUST have a `ClientID`. `cancelOwner()` is the primary guard against AI runaway.
-   **Monotonic States**: `JobStatus` only moves forward. No state desync.
-   **Append-Only Results**: Results are never overwritten; only appended (streaming) or finalized.

## 🤖 SME Validation Checklist
- [ ] **No Qt**: Does this header include `<QObject>` or any Qt type? (Forbidden: Use STL or POD DTOs).
- [ ] **No Filesystem**: Does this API take a filesystem path string? (Forbidden: Use DTO boat-loading or memory-mapped handles).
- [ ] **Immutability**: Are results returned as non-const pointers? (Forbidden: Must use `shared_ptr<const SimulationResponseDTO>`).
- [ ] **Ownership**: Does the submission call lack a `ClientID`? (Forbidden: AI jobs must be attributable for cancellation).
- [ ] **ABI Stability**: If changing a bridge, did you update the `engineAbiHash`?
