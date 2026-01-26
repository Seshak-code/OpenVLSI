# AcuteSim/CuteApp Codebase Context

## 🌟 Project Vision
**AcuteSim** (Engine) and **CuteApp** (Application) form an AI-native EDA platform for ASIC design (RTL->GDS). It aims to transition from a standalone simulator to an **Agent-Centric Cognitive Foundry**.

## 🏛 Structural Overview

### 1. GUI / Frontend (`gui/`)
- **VS Code Paradigm**: Built with Qt 6, featuring a clean, modern dockable UI.
- **Schematic Editor**: Custom `QGraphicsScene` for circuit design with sub-pixel snapping and real-time ERC (Engineering Rule Checks).
- **AI Assistant Panel**: Direct interface for the Copilot, supporting "Visual Diffs" of suggested circuit changes.
- **Mode Selection**: Context-aware stages (Bench, Block, System Integration).
- **Interconnections**:
    - Consumes `compute/orchestration` for simulation control.
    - Uses `compute/infrastructure/simtrace.h` for visualizing results (`WaveformViewer`).
    - *Never* talks directly to `physics/` or `compute/math/`.

### 2. Compute / Physics Engine (`compute/`, `physics/`)
- **MNA Tensor Backend**: Custom Modified Nodal Analysis using **SoA (Structure of Arrays)** (`compute/tensors/`) for GPU/SIMD optimization.
- **Dual Kernel Strategy** (`physics/`): Every device (BJT, Diode) has an `exact` model for KCL auditing and a `clamped` model for Newton-Raphson stability.
- **Hybrid Solver** (`compute/solvers/`): Automatically selects between **LU Decomposition** (robustness) and **PCG** (speed).
- **Infrastructure** (`compute/infrastructure/`):
    - `tensorscheduler.h`: Batches jobs by **Topology Hash**.
    - `safe_utils.h`: Enforces bounds checking and NaN scrubbing.
    - `simtrace.h`: Records execution metadata (iterations, solver method) alongside results.

### 3. Verification & Regression (`tests/`)
- **Regression DB**: `regression_golden.db` stores "Golden" reference results.
- **Interconnections**:
    - `regression_db.h` loads/saves the golden DB.
    - `regression_suite.h` runs simulations via `CircuitSim` and compares against the DB.
    - **CI Flow**: `.github/workflows/ci.yml` runs these tests on Windows, MacOS, Linux, and WASM.

### 4. AI Cognitive Layer (`ai_api/`)
- **Speculative Runner**: Forks a headless simulation "clone" to test AI hypotheses.
- **Simulation Trace**: Structured JSON knowledge objects that ground the AI in physical reality.
- **Failure Taxonomy**: Automated classifier that labels crashes (e.g., `FLOATING_NODE`) for better AI reasoning.

## 🔬 Deep Architecture & Data Flow (The "Pipeline")
The system follows a strict **Data-Oriented Design (DoD)** pipeline where dependencies flow downwards.

### 1. Data Ingestion & Transformation (`components/` -> `compute/tensors/`)
-   **Structure**: `components/circuit.h` (`TensorNetlist`) holds the user-facing "Array of Structures".
-   **Transformation**: `physics_tensors.cpp` converts this into **SoA (Structure of Arrays)** (`TensorizedBlock`) for SIMD efficiency.
-   **Batching**: `tensorscheduler.h` groups jobs by `topologyHash` to enable batch processing.

### 2. The Solver Engine (`compute/solvers/` -> `physics/`)
-   **Strategy**: `circuitsim_solver.cpp` manages the Newton-Raphson loop and selects the backend (`solveLU` vs `solvePCG`).
-   **Stamping**:
    -   `physics/circuitsim.h` orchestrates the stamping.
    -   **Passive**: `stampBlockLinear()` handles R/L/C using direct matrix writes.
    -   **Active**: `stampSoABlock()` dispatches **Vectorized Kernels** (`physics_tensors.h`) to compute device physics in batches.
-   **Dual Kernel**: `physics/device_physics.h` provides both `exact` (truth) and `clamped` (stable) physics models.

### 3. Numerical Kernel (`compute/math/`)
-   **Linear Algebra**: `linalg.h` provides Sparse Matrix (CSR) storage and solvers (`solvePCG`, `solveLU_Pivoted`).
-   **Integration**: `integration_method.h` abstracts time-stepping (Trapezoidal, Gear-2).
-   **Validation**: `emulated_f64.h` mocks GPU precision for determining algorithm correctness.

### 4. Verification & Output (`compute/infrastructure/` -> `gui/`)
-   **Safety**: `SafeUtils` scrubs NaNs; `calculatePhysicalResiduals` enforces KCL conservation (< 1pA).
-   **Tracing**: `simtrace.h` captures "Trust Surface" data (residuals, iteration counts) for the GUI.
-   **Visualization**: The GUI (`WaveformViewer`) renders these traces, linking back to the `Roadmap.md` phase that generated them.

## 🧩 Standard Agentic Development Workflow
The project enforces a strict "Context-First" development loop via `/Users/seshak/.agent/workflows/agentic_flow.md`:
1. **Analyze Roadmap**: Ground every task in an M1-M15 milestone or strategy.
2. **Flow Grounding**: Read the `flow.md` for *every* folder touched. If it creates/modifies files, you **MUST** consult the "🤖 SME Validation Checklist" in that `flow.md` before proceeding. If the `flow.md` doesn't exist, create it.
3. **Structured Planning**: Refer explicitly to constraints in `flow.md` (e.g., "Dual Kernel separation").
4. **Approval Loop**: Human-in-the-loop validation of `implementation_plan.md`.
5. **Trust Surface Verification**: Validate against physical conservation and residuals.
