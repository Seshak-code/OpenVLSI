# Tests Flow (Optional)

The `tests/` directory contains the **Verification Framework**. It enforces the correctness of the physics engine and the stability of the UI logic.

## Test Suites

### 1. Physics Verification
-   **`test_simulation.cpp`**: Comprehensive SPICE netlist verification (OpAmps, 555 Timers).
-   **`test_sensitivity.cpp`**: Checks AutoDiff gradients against finite differences.
-   **`regression_golden.db`**: JSON-structured database storing "Golden" results with Metadata (OS, Compiler) for cross-platform regression.
-   **`test_physics_kernels.cpp`**: Comprehensive verification of BJT, MOSFET, and Diode physics kernels (Double + EmulatedF64).
-   **`test_emulated_f64.cpp`**: Verifies compatibility of device physics templates with Emulated F64 (GPU double-double) types.
-   **`test_dual_emulated.cpp`**: Verifies that Automatic Differentiation (`Dual<T>`) functions correctly with `EmulatedF64`.
-   **`test_fpu_determinism.cpp`**: Enforces strict IEEE-754 rounding (Nearest-Even) and subnormal handling across platforms.
-   **`test_kernel_dispatch.cpp`**: Verifies that `KernelDispatcher` correctly binds and executes the optimal SIMD path (Neon/AVX/Generic).

### 2. UI Integration
-   **`test_gui_logic.cpp`**: Headless testing of `SchematicEditor` interactions (Add, Move, Undo).
-   **`test_docking_interaction.cpp`**: Verifies that panels dock/undock correctly without crashing.

### 3. AI & Orchestration
-   **`test_ai_simulation_intelligence.cpp`**: Validates `SpeculativeRunner` and `FailureTaxonomy`.
-   **`test_copilot_validate.cpp`**: End-to-end flow from "Prompt" to "Circuit Change".

## 🤖 SME Validation Checklist
*(Consult this list before modifying `tests/`)*

- [ ] **Golden Data**: Are you hardcoding result values? (Forbidden: Use `regression_golden.db` unless writing a micro-unit test).
- [ ] **Determinism**: Does the test enforce `FPUControl::enforceDeterminism()`?
- [ ] **Cross-Platform**: Does the test rely on specific file paths (e.g. `C:\` or `/home`)? (Forbidden: Use relative paths/CMake definitions).
- [ ] **Coverage**: Does the new test cover the specific `flow.md` constraint of the module tested?
- [ ] **Performance**: Does the test execute in <100ms? (If not, move to `benchmark_suite.h`).

## Running Tests
Tests are registered with `CTest`.
```bash
cd build
ctest --verbose
```
