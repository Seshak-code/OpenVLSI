# AI API Flow: The Cognitive Architecture

The `ai_api/` directory implements the **Artificial Engineer** that sits on top of the AcuteSim physics engine. It is not just a chatbot wrapper; it is a structured cognitive system with distinct modules for sensing, reasoning, acting, and remembering.

## The Cognitive Stack

The system is organized into 6 functional layers, each backed by a sub-directory and a corresponding `flow.md`.

### 1. Senses (`ai_api/system`)
**Function**: The Interface to Physical Reality.
**Role**: Handles geometric manipulation (`SchemaAPI`), feels pain (`CrashReport`), and learns from feedback (`FeedbackLoop`).
**Ground Truth**: `schema_api.h`

### 2. Perception (`ai_api/analysis`)
**Function**: Semantic Understanding.
**Role**: Translates raw voltages/netlists into engineering concepts ("Differential Pair", "Floating Node").
**Ground Truth**: `topology_analyzer.h`

### 3. Memory (`ai_api/context`)
**Function**: The Hippocampus.
**Role**: Manages short-term context (Prompts) and long-term knowledge (RAG Vector Store).
**Ground Truth**: `rag_context.h`

### 4. Imagination (`ai_api/simulation`)
**Function**: Speculative Execution.
**Role**: Runs isolated "What-If" simulations to test hypotheses before suggesting them.
**Ground Truth**: `speculative_runner.h`

### 5. Action (`ai_api/core`)
**Function**: The Motor Cortex.
**Role**: Dispatches structured JSON commands to the workbench and parses user intent.
**Ground Truth**: `ai_interface.h`

### 6. Conscience (`ai_api/policy`)
**Function**: Safety & Grounding.
**Role**: Enforces "Evidence-First" protocols and validates citations.
**Ground Truth**: `ai_policy.h`

## The Introspection Layer

At the center of this stack lies the **Introspection API**, defined in `introspection_api.h`. This is the single source of truth for the "State of the World."

```cpp
// ai_api/introspection_api.h
struct CircuitState {
    // Identity
    string name;
    uint64_t topologyHash;
    DesignMode mode; // BENCH, BLOCK, SYSTEM
    
    // Physical Reality
    vector<double> nodeVoltages; // The ultimate ground truth
    double residual;             // How much physics is violated
    
    // Status
    bool converged;
    int drcErrors;
    
    // Serialization
    string toJSON() const;
};
```

## The Cognitive Loop

When a user says "Fix the crash":

1.  **Sensing**: `introspection_api` captures the `CircuitState` and `CrashReport`.
2.  **Perception**: `analysis` labels the crash as "Singular Jacobian" and identifies the "Darlington Pair" topology.
3.  **Memory**: `context` retrieves similar past crashes from the RAG store.
4.  **Imagination**: `simulation` forks the netlist, adds a GMIN resistor, and verifies convergence.
5.  **Action**: `core` generates a `placeComponent` command for the resistor.
6.  **Conscience**: `policy` ensures the explanation cites the specific GMIN log lines.
7.  **Output**: "I fixed the singularity by adding a 1GΩ resistor (GMIN). Sim converged [Line 42]."

## Development Rules

1.  **Hierarchy**: Dependencies flow DOWN. `core` can use `analysis`, but `analysis` acts on raw data and shouldn't depend on `core`.
2.  **Grounding**: All layers must ultimately reference `introspection_api.h` or `geometry_api.h` (physics/schema).
3.  **Flow Compliance**: Every sub-folder MUST have a `flow.md` that links back to this master architecture.
