# Tests Flow (Optional)

The `tests/` directory contains the **Verification Framework**. It enforces the correctness of the physics engine and the stability of the UI logic.

## Test Suites

### 1. Physics Verification
-   **`test_simulation.cpp`**: Comprehensive SPICE netlist verification (OpAmps, 555 Timers).
-   **`test_sensitivity.cpp`**: Checks AutoDiff gradients against finite differences.
-   **`regression_golden.db`**: SQLite database storing "Golden" results for regression tracking.

### 2. UI Integration
-   **`test_gui_logic.cpp`**: Headless testing of `SchematicEditor` interactions (Add, Move, Undo).
-   **`test_docking_interaction.cpp`**: Verifies that panels dock/undock correctly without crashing.

### 3. AI & Orchestration
-   **`test_ai_simulation_intelligence.cpp`**: Validates `SpeculativeRunner` and `FailureTaxonomy`.
-   **`test_copilot_validate.cpp`**: End-to-end flow from "Prompt" to "Circuit Change".

## Running Tests
Tests are registered with `CTest`.
```bash
cd build
ctest --verbose
```
