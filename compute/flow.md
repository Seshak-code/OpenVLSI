# Compute Module Flow

The `compute/` directory is the **Execution Engine** of AcuteSim. It transforms the static netlist (`components/`) into dynamic simulation results.

It is architected as a **Data-Oriented Pipeline**, moving away from Object-Oriented traversals to maximize CPU cache efficiency and parallelism.

## Architecture Guidelines

### The Layered Model
Dependencies flow **downwards**. A layer may only import from layers below it.

| Layer | Directory | Responsibility | Key Abstraction |
| :--- | :--- | :--- | :--- |
| **5. Control** | `orchestration/` | External Tool Management | `WorkerClient` |
| **4. Strategy** | `solvers/` | Physics Orchestration | `CircuitSim::solveDC` |
| **3. Reliability** | `infrastructure/` | Safety & Tracing | `SafeUtils`, `SolverTrace` |
| **2. Operations** | `math/` | Numerical Kernels | `solvePCG`, `solveLU`, `EmulatedF64` |
| **1. Data** | `tensors/` | Memory Layout | `TensorizedBlock` (SoA) |

## The Simulation Pipeline (Ground Truth)

When `CircuitSim::solveDC()` is called, the following sequence occurs:

1.  **Preparation (AoS -> SoA)**
    *   **Source**: `components/circuit.h` (`TensorNetlist`).
    *   **Action**: `physics_tensors.cpp` convert the friendly object graph into a `TensorizedBlock` (Structure-of-Arrays).
    *   **Optimization**: `tensorscheduler.h` groups hierarchical instances by `topologyHash` into `SolverJob` batches.

2.  **The Solver Loop (`solvers/circuitsim_solver.cpp`)**
    *   **Seeding**: `seedInitialGuess` presets voltage vectors based on Diode/BJT models.
    *   **Newton-Raphson**: The main `while(residual > tol)` loop.
    *   **Stamping (`physics/circuitsim.h`)**:
        *   `stampAllElements()` is the entry point.
        *   It calls `stampBlockLinear()` for passive R/L/C/V/I elements.
        *   It calls `stampSoABlock()` to dispatch vectorized kernels (`batchDiodePhysics`) for active devices.
        *   It iterates `scheduler.jobs` to stamp sub-circuits efficiently.

3.  **Matrix Solution (`math/linalg.h`)**
    *   **Assembly**: `MatrixConstructor` sums overlapping stamps (KCL Superposition) and compresses to `Csr_matrix`.
    *   **Solve**: The engine selects `solvePCG` (Iterative, O(N)) for large passive grids or `solveLU_Pivoted` (Direct, O(N³)) for active/small circuits.
    *   **Hardware**: (Experimental) `solvers/webgpu_solver.h` offloads this entire step to WGSL compute shaders.

4.  **Verification (`infrastructure/`)**
    *   **Safety**: `SafeUtils::sanitize_vector` scrubs NaNs.
    *   **Audit**: `calculatePhysicalResiduals` ensures $ \sum I = 0 $ at every node, preventing "mathematically correct but physically wrong" convergence.
    *   **Trace**: `simtrace.h` captures the result snapshot for the GUI.

## Code Map

### The "Hot Path"
These files run inside the inner loop (MHz rates). Do not allocate memory here.
*   `compute/tensors/physics_tensors.h` (SoA storage)
*   `compute/math/linalg.h` (SpMV, Vector Add)
*   `physics/circuitsim.h` (`stampAllElements`)

### The "Control Path"
These files run once per simulation or user click.
*   `compute/solvers/circuitsim_solver.cpp` (Convergence logic)
*   `compute/orchestration/grpc_worker_client.cpp` (RPC dispatch)
*   `compute/infrastructure/tensorscheduler.h` (Job Batching)

## Integration Points

*   **GUI Integration**: The GUI communicates primarily with `orchestration/` (for synthesis) or `solvers/` (for simulation), never directly with `math/` or `tensors/`.
*   **Physics Integration**: `solvers/` depends on `physics/device_physics.h` logic, but the *storage* of device state is in `tensors/`.
