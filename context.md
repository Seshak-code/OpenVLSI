# AcuteSim/CuteApp Codebase Context

## 🌟 Project Vision
**AcuteSim** (Engine) and **CuteApp** (Application) form an AI-native EDA platform for ASIC design (RTL->GDS). It aims to transition from a standalone simulator to an **Agent-Centric Cognitive Foundry**.

## 🏛 Structural Overview & Interconnections

### 1. GUI / Frontend (`gui/`)
The frontend is built with **Qt 6**, focusing on a modern, dockable VS Code-like paradigm.
-   **Visual Primitives** (`gui/components/`):
    -   `VisualComponent` (`visual_component.h`): The base class for all renderable items. It now supports a **Unified Component Library** (no separate digital/analog modes) and **Ghosting** for AI suggestions.
    -   `ProbeTip` (`probe_tip.h`): Implements "Oscilloscope Style" drag-and-drop measurement probes that snap to wires.
-   **Editors** (`gui/editors/`):
    -   `SchematicEditor` (`schematic_editor.cpp`): Handles the canvas logic. Crucially, it manages **Spatial Connectivity Analysis** (merging wires via union-find) and **Netlist Generation** (converting visual items to `TensorNetlist`). It explicitly wires PDK primitives (NFET/PFET) to 4 terminals.
-   **Interconnections**:
    -   Outputs `TensorNetlist` to `components/` for structural definition.
    -   Consumes `SimulationTrace` from `compute/` for visualization in `WaveformViewer`.

### 2. Structural Layer (`components/`)
Defines *what* the circuit is, independent of physics or visual representation.
-   **Core Definitions**: `circuit.h` defines `TensorBlock` (immutable definition) and `BlockState` (mutable history).
-   **Canonicalization** (`graph_canonical.h`): Implements orientation-invariant hashing (BFS from Ground) to detect topologically identical circuits.
-   **Difference Engine** (`netlist_diff.h`): Computes semantic diffs (Structure vs Parameter changes) to optimize re-simulation.

### 3. Compute Engine (`compute/`)
The execution engine follows a **Data-Oriented Pipeline**, moving data downwards from simple structs to SIMD-optimized tensors.
-   **Control** (`compute/orchestration/`): Manages external tools and cloud dispatch (`WorkerClient`).
-   **Strategy** (`compute/solvers/`): Orchestrates the Newton-Raphson loop (`circuitsim_solver.cpp`).
-   **Math Kernels** (`compute/math/`): Provides `solvePCG` (Iterative) and `solveLU` (Direct) solvers using `linalg.h`.
-   **Tensors** (`compute/tensors/`): Stores simulation data in **Structure-of-Arrays (SoA)** format for AVX-512/GPU efficiency (`physics_tensors.h`).

### 4. Physics Core (`physics/`)
Implements a **Dual Kernel** architecture for numerical robustness (`physics/flow.md`).
-   **Physical Truth** (`device_physics.h`): Contains exact constitutive equations (stateless).
-   **Numerical Stability**: Wraps truth models with **Clamping** (e.g., `pnjlim`) and **Linear Continuation** to prevent solver divergence.
-   **Stamping** (`stamps.h` -> `circuitsim.h`): Maps physical quantities to the solver matrix. Uses "Norton Equivalents" for voltage sources to minimize matrix size.

## ✅ Recent Implementation Status

### 1. Unified Component Library & PDK Compliance (Phase 1-5 Completed)
The legacy "Digital vs Analog" component split has been removed.
-   **Unified Enum**: `ComponentType` (`visual_component.h`) serves as the single source of truth.
-   **PDK Primitives**: `PDK_NFET`, `PDK_PFET`, `PDK_Resistor`, `PDK_Capacitor` map to foundry models (e.g., "nfet_01v8").
-   **Standard Cells**: Logic gates (Inverter, NAND) in `standard_cells.h` are now composed of explicitly parameterized PDK MOSFETs, ensuring RTL-to-GDS flow compatibility.
-   **PDK Integration**: `SchematicEditor` netlisting extracts geometry (W, L, Area) and Model names for *all* components, defaulting to generic PDK values if unspecified.

### 2. Parasitic-Aware Netlisting (Phase 1.5 Implemented)
-   **VisualWire**: Now carries physical attributes: `layer` (M1..M6), `width_um`, and `model_name`.
-   **Netlist Extraction**: `SchematicEditor::generateNetlist` iterates wires to extract `WireSegment` data (Length, Layer) and injects `WireSegment` and `ParasiticResistor` structs into the `TensorNetlist`.
-   **SPEF Readiness**: The `TensorNetlist` structure (`circuit.h`) now supports `wireSegments` and `parasiticResistors`, ready for future SPEF export.

### 3. Oscilloscope-Style Probes (Implemented)
-   **Visuals**: `ProbeTip` classes allow draggable positive/negative tips that snap to wires (`visual_component.h`).
-   **Logic**: `SchematicEditor` implements specific logic to "Find Nearest Terminal Node" on wires (`schematic_editor.cpp:1135`) and inject `Probe` objects into the netlist *after* stamping logic.

## 🔬 Deep Data Flow (The Pipeline)

1.  **User/AI Action**: User places a `PDK_NFET` in the `SchematicEditor`.
2.  **Netlisting**: `GridScene::generateNetlist` runs spatial union-find to determine connectivity and outputs a `TensorNetlist`.
3.  **Transformation**: `physics_tensors.cpp` converts the netlist from **AoS** (Circuit objects) to **SoA** (aligned vectors).
4.  **Solver Loop**: `CircuitSim` starts the Newton-Raphson process:
    -   **Stamping**: Calls `stampSoABlock` to evaluate device physics in parallel batches.
    -   **Solve**: Calls `solveLU` or `solvePCG` to find $\Delta V$.
    -   **Check**: `calculatePhysicalResiduals` validates KCL conservation (< 1pA).
5.  **Visualization**: `SimulationTrace` is sent back to the GUI, where `WaveformViewer` renders the results.

## 🧩 Standard Agentic Development Workflow
The project enforces a strict "Context-First" development loop via `/Users/seshak/.gemini/antigravity/global_workflows/agentic_flow.md`:
1.  **Analyze Roadmap**: Ground every task in an M1-M15 milestone or strategy.
2.  **Flow Grounding**: Read the `flow.md` for *every* folder touched. If it creates/modifies files, you **MUST** consult the "🤖 SME Validation Checklist" in that `flow.md` before proceeding.
3.  **Structured Planning**: Refer explicitly to constraints in `flow.md` (e.g., "Dual Kernel separation").
4.  **Approval Loop**: Human-in-the-loop validation of `implementation_plan.md`.
5.  **Trust Surface Verification**: Validate against physical conservation and residuals.
