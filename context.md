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

## 🔗 Critical Interconnections (The "Flow")
1.  **User Edit**: `gui/` -> `SchematicModel` -> `components/circuit.h` (`TensorNetlist`).
2.  **Simulation Request**: `gui/` -> `compute/orchestration/` -> `compute/solvers/`.
3.  **Data Transformation**: `TensorNetlist` (AoS) -> `compute/tensors/physics_tensors.h` (SoA).
4.  **Execution**: `compute/solvers/` -> `physics/circuitsim.h` -> `physics/device_physics.h` (Kernels).
5.  **Verification**: Result -> `compute/infrastructure/simtrace.h` -> `gui/` (Waveforms).

## 🧩 Standard Agentic Development Workflow
The project enforces a strict "Context-First" development loop via `/Users/seshak/.agent/workflows/agentic_flow.md`:
1. **Analyze Roadmap**: Ground every task in an M1-M15 milestone or strategy.
2. **Flow Grounding**: Read the `flow.md` for *every* folder touched. If it doesn't exist, create it.
3. **Structured Planning**: Refer explicitly to constraints in `flow.md` (e.g., "Dual Kernel separation").
4. **Approval Loop**: Human-in-the-loop validation of `implementation_plan.md`.
5. **Trust Surface Verification**: Validate against physical conservation and residuals.
