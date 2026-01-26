# Compute Infrastructure Flow

The `compute/infrastructure/` directory encapsulates the **Orchestration, Reliability, and Verification** layers of the simulation engine. It does not solve equations (that's `physics/`) or optimize structure (that's `components/`), but rather *manages* the execution of those tasks.

It serves as the **Operational/Safety Supervisor** ensuring that:
1.  **Orchestration**: Jobs are batched and routed to the best hardware (`tensorscheduler.h`).
2.  **Reliability**: Memory access is bounds-checked and math is sanitized (`safe_utils.h`).
3.  **Verification**: Simulation runs produce reproducible, traceable artifacts (`simtrace.h`).
4.  **Sensitivity**: Designs are stress-tested against variance (`monte_carlo.h`).

## Key Files & Responsibilities

### 1. `tensorscheduler.h`: The Orchestrator
Manages the execution queue. It groups circuit blocks not just by name, but by **Topology Hash**.
*   **Feature**: `schedule(netlist)` groups identical subcircuits (isomorphic graphs) into a single `SolverJob` for SIMD/Batch processing.
*   **Dependency Inversion**: Uses `IHardwareDetector` to select between `CPU_SEQUENTIAL`, `CPU_OPENMP`, or `WEBGPU_COMPUTE` at runtime without hard verification coupling.

### 2. `safe_utils.h`: The Safety Net
Implements "NASA/JPL-Style" defensive programming.
*   **Principle**: "Crash early in Debug, Survive safely in Release."
*   **`safe_at`**: Vector access that throws informative exceptions for out-of-bounds (OOB) errors.
*   **`sanitize_value`**: Scrubs NaN/Inf values to prevent poison propagation in the solver.

### 3. `simtrace.h`: The Black Box Recorder
Decouples simulation *execution* from *result storage*.
*   **Purpose**: Enables headless runs, GUI replays, and AI analysis.
*   **Trust Metadata**: Records *how* the result was achieved (Integration method, GMIN stepping usage, Convergence iterations).
*   **Provenance**: Every `SolverStep` carries the `topologyHash` of the circuit at that moment, ensuring results map back to the correct structure even if it changes.

### 4. `monte_carlo.h`: The Stress Tester
Wraps the solver in a statistical loop.
*   **Logic**: Creates deep copies of `TensorNetlist` with Gaussian-perturbed parameters (3-Sigma rule).
*   **Output**: Produces statistical distributions (Mean, StdDev) rather than single scalar voltages.

## Core Logic & Snippets

### Topology-Based Batching (`tensorscheduler.h`)
This allows the engine to solve 1000 "Voltage Dividers" as a single batch job, even if they are named differently, provided they are structurally identical.

```cpp
void schedule(const TensorNetlist& netlist) {
    // Group instances by their Canonical Topology Hash
    std::map<size_t, std::vector<int>> groupedByHash;
    
    for (int i = 0; i < (int)netlist.instances.size(); ++i) {
        const auto& inst = netlist.instances[i];
        // Look up structure definition to get hash
        auto it = netlist.blockDefinitions.find(inst.blockName);
        if (it != netlist.blockDefinitions.end()) {
            groupedByHash[it->second.topologyHash].push_back(i);
        }
    }
    // ... create jobs from groups ...
}
```

### Defensive Access (`safe_utils.h`)
Used everywhere in the solver inner loops to prevent segfaults.

```cpp
template<typename T>
inline T& safe_at(std::vector<T>& vec, size_t index, const char* context = "") {
#ifndef NDEBUG
    // Debug: Hard crash to alert developer
    assert(index < vec.size() && "safe_at: Index out of bounds");
#endif
    if (index >= vec.size()) {
        // Release: Throw handled exception
        throw std::out_of_range("Index " + std::to_string(index) + " out of bounds");
    }
    return vec[index];
}
```

## Development Rules

1.  **Never access vectors directly**: In compute-heavy loops, use `SafeUtils::safe_at` or `safe_idx`. 
2.  **Trust but Verify**: Every simulation result must be wrapped in a `SolverStep` with full metadata (method, iteration count). A result without metadata is untrusted.
3.  **Topology is Identity**: The Scheduler groups by `topologyHash`. If you modify `components/graph_canonical.h`, verify `tensorscheduler.h` still groups correctly.
4.  **Hardware Agnosticism**: The Scheduler asks `IHardwareDetector` what to do. Do not hardcode `#ifdef` for hardware selection inside the solver logic itself; inject the strategy.

## 🤖 SME Validation Checklist
*(Consult this list before modifying `compute/infrastructure/`)*

- [ ] **Bounds Safety**: Are you using `safe_at` or `safe_idx` for all vector accesses in the loop? (Forbidden: `vec[i]` without asserts).
- [ ] **Provenance**: Does the new simulation step record valid `topologyHash` and `integrationMethod` metadata?
- [ ] **NaN Scrubbing**: Are inputs from `physics/` passed through `SafeUtils::sanitize`?
- [ ] **Topology Hash**: If you modified grouping logic, did you verify that isomorphic subcircuits still hash identically?
