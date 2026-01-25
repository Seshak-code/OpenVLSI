# AI Simulation Flow

The `ai_api/simulation/` directory is the **Speculative Engine** logic. It enables the AI to "think" by running isolated simulations of potential designs before suggesting them to the user.

## Key Files & Responsibilities

### 1. Speculative Runner (`speculative_runner.h`)
**Role**: The "Sandbox". Runs "What-If" scenarios without affecting the user's active workbench.
-   **Isolation**: Clones the base netlist to ensure safety.
-   **Overrides**: Applies specific changes (e.g., GMIN stepping, time step) to test convergence hypotheses.

```cpp
// ai_api/simulation/speculative_runner.h
/**
 * Run a speculative simulation based on a base netlist and a set of overrides.
 * This method is CONST-safe regarding the input netlist (it clones it).
 */
static SimulationTrace runSpeculative(
    const TensorNetlist& baseNetlist,
    const SpeculativeOverrides& overrides,
    double simulationDuration = 1e-6
);
```

### 2. Simulation Trace (`simulation_trace.h`)
**Role**: The "Memory Unit". Defines the schema for a simulation run, which serves as the fundamental unit of knowledge.
-   **TraceFailure**: Captures specific failure modes (e.g., "Singular Jacobian") to ground the AI's error analysis.
-   **RAG Chunks**: Pre-computes embedding-ready chunks for future retrieval.

```cpp
// ai_api/simulation/simulation_trace.h
struct SimulationTrace {
    QString traceId;
    QString topologyHash;    // Canonical ID of the circuit structure
    QVector<TraceFailure> failureModes; // If empty, simulation was successful
    
    // RAG Data
    QVector<TraceChunk> ragChunks;
};
```

### 3. Vector Store (`local_vector_store.h`)
**Role**: The "Hippocampus". Manages the storage and retrieval of past experiences.
-   **Counterfactual Lookup**: Finds traces with the *same* topology but *different* configurations to answer "What if?" questions.
-   **Similarity Search**: Retrieval of similar past failures to guide current debugging.

```cpp
// ai_api/simulation/local_vector_store.h
/**
 * "What-If" Retrieval: Find traces with same topology but different config
 * (e.g., "Show me this circuit with GMIN stepping enabled")
 */
virtual QVector<SimulationTrace> findCounterfactuals(
    const QString& topologyHash, 
    const TraceConfig& targetConfig
) = 0;
```

### 4. Auto Validator (`auto_harness.h`)
**Role**: The "Proving Ground". automated test harness for verifying sub-blocks.
-   **Stimulus Injection**: Automatically connects pulse/sine sources to input ports.
-   **Probe Verification**: Checks output ports against expected logic levels.

## Data Flow
1.  **Hypothesis**: The AI proposes a change (e.g., "Increase R1 to 10k").
2.  **Speculation**: `SpeculativeRunner` clones the netlist, applies the change, and runs a short transient sim.
3.  **Trace Capture**: The result is captured as a `SimulationTrace`.
4.  **Storage**: The trace is upserted into `LocalVectorStore`.
5.  **Retrieval**: If the user asks "Has this happened before?", the system queries the store for similar topology hashes.

## Development Rules
1.  **Const Correctness**: `SpeculativeRunner` MUST NEVER modify the original `TensorNetlist`.
2.  **Trace Immutability**: Once a `SimulationTrace` is created and stored, it is immutable ID-based history.
3.  **Resource Bounding**: Speculative runs limited to 1e-6s duration (by default) to prevent UI freezing.
