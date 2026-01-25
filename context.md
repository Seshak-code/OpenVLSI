# AcuteSim/CuteApp Codebase Context

## 🌟 Project Vision
**AcuteSim** (Engine) and **CuteApp** (Application) form an AI-native EDA platform for ASIC design (RTL->GDS). It aims to transition from a standalone simulator to an **Agent-Centric Cognitive Foundry**.

## 🏛 Structural Overview

### 1. GUI / Frontend (`gui/`)
- **VS Code Paradigm**: Built with Qt 6, featuring a clean, modern dockable UI.
- **Schematic Editor**: Custom `QGraphicsScene` for circuit design with sub-pixel snapping and real-time ERC (Engineering Rule Checks).
- **AI Assistant Panel**: Direct interface for the Copilot, supporting "Visual Diffs" of suggested circuit changes.
- **Mode Selection**: Context-aware stages (Bench, Block, System Integration).

### 2. Compute / Physics Engine (`compute/`, `physics/`)
- **MNA Tensor Backend**: Custom Modified Nodal Analysis using **SoA (Structure of Arrays)** for GPU/SIMD optimization.
- **Dual Kernel Strategy**: Every device (BJT, Diode) has an `exact` model for KCL auditing and a `clamped` model for Newton-Raphson stability.
- **Hybrid Solver**: Automatically selects between **LU Decomposition** (robustness for active circuits) and **PCG** (speed for large passive grids).
- **Regression DB**: [regression_golden.db](file:///Users/seshak/Desktop/CuteApp/regression_golden.db) stores bit-exact reference results across x86, ARM, and WASM.

### 3. AI Cognitive Layer (`ai_api/`)
- **Speculative Runner**: Forks a headless simulation "clone" to test AI hypotheses without affecting the user's workbench.
- **Simulation Trace**: Structured JSON knowledge objects (not just text logs) that ground the AI in physical reality.
- **Failure Taxonomy**: Automated classifier that labels crashes (e.g., `FLOATING_NODE`) for better AI reasoning.
- **Topology Analyzer**: Pattern matching to identify functional blocks like "Darlington Pair" or "Current Mirror".

### 4. Orchestration & ASIC Flow (`compute/orchestration/`)
- **Driver Model**: Translation of design intent into tool-specific scripts (Yosys, OpenROAD, Klayout).
- **gRPC Infrastructure**: Connects the local app to containerized workers or cloud compute.

## 📁 Key Documentation Path
- **Foundational Guide**: `AI_copilot_complete_guide.txt` (Thesis on numerical hardening and AI grounding).
- **Vision/Roadmap**: `Roadmap.md` (Strategic direction and shipment gates).
- **Architecture**: `context.md` (Module-level structure).
- **Workflows**: `agentic_flow.md` (The standard procedure for developing this project).

## 🧩 Standard Agentic Development Workflow
The project enforces a strict "Context-First" development loop via `/Users/seshak/.agent/workflows/agentic_flow.md`:
1. **Analyze Roadmap**: Ground every task in an M1-M15 milestone or strategy.
2. **Flow Grounding**: Read the `flow.md` for *every* folder touched. If it doesn't exist, create it.
3. **Structured Planning**: Refer explicitly to constraints in `flow.md` (e.g., "Dual Kernel separation").
4. **Approval Loop**: Human-in-the-loop validation of `implementation_plan.md`.
5. **Trust Surface Verification**: Validate against physical conservation and residuals.
